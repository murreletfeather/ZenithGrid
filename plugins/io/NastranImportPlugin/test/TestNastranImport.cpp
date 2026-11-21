#include "ComponentData.h"
#include "MeshData.h"
#include "ModelData.h"
#include "ModelIOSystem.h"
#include "ModelLayer.h"
#include "ModelObserver.h"
#include "NastranImportHandler.h"
#include "NastranReader.h"
#include "TempFile.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace systems::io;

namespace {

const systems::job::ProgressFn no_progress = [](double, const std::string&) { };

constexpr std::string_view triangle_deck = "CTRIA3,700,2,10,20,30\n"
                                           "GRID,10,,0,0,0\n"
                                           "GRID,20,,1,0,0\n"
                                           "GRID,30,,0,1,0\n";

fs::path writeDeck(std::string_view contents, const fs::path& extension = ".nas")
{
    auto path = core::TempFile::instance().path();
    path.replace_extension(extension);
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output << contents;
    REQUIRE(output);
    return path;
}

std::string fixedLine(std::string_view keyword, std::size_t width,
    std::initializer_list<std::string_view> fields, std::string_view next = {})
{
    std::string line(keyword);
    line.resize(8, ' ');
    for (const auto field : fields) {
        REQUIRE(field.size() <= width);
        line.append(width - field.size(), ' ');
        line.append(field);
    }
    if (!next.empty()) {
        REQUIRE(line.size() <= 72);
        line.resize(72, ' ');
        line.append(next);
    }
    return line + "\r\n";
}

std::string readError(const fs::path& path)
{
    try {
        readNastranMesh(path, no_progress);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    FAIL("Expected the Nastran reader to reject the deck");
    return {};
}

void checkErrorLocation(const std::string& error, const fs::path& path, std::size_t line,
    std::string_view card)
{
    CHECK(error.find(path.filename().string() + ":" + std::to_string(line)) != std::string::npos);
    CHECK(error.find(card) != std::string::npos);
}

class ImportedModelObserver final : public ModelObserver {
public:
    void notifyModelAdded(Index model_id) override
    {
        added = model_id;
    }
    void notifyModelChanged(Index) override { }
    void notifyComponentChanged(Index) override { }
    void notifyModelRemoved(Index) override { }
    void notifyComponentRemoved(Index) override { }
    void notifyModelNameChanged(Index, const std::string&) override { }
    void notifyGeometryLoadFailed(const std::string&) override { }

    Index added { -1 };
};

}

TEST_CASE("Nastran imports shell and solid elements with shared local nodes", "[NastranImport]")
{
    const auto path = writeDeck(
        "APP DISP\r\n"
        "SOL 101\r\n"
        "CEND\r\n"
        "TITLE = mixed elements\r\n"
        "BEGIN BULK\r\n"
        "CTRIA3,71,4,90,21,41\r\n"
        "CQUAD4,72,4,90,21,11,64\r\n"
        "CTETRA,73,899,64,11,42,98\r\n"
        "CPENTA,74,900,90,21,41,64,11,42\r\n"
        "GRID,90,,0,0,0,99\r\n"
        "GRID,21,,1.0D+0,0,0\r\n"
        "GRID,41,,0,1.+0,0\r\n"
        "GRID,64,,0,0,10.-1\r\n"
        "GRID,11,,1,0,1\r\n"
        "GRID,42,,0,1,1\r\n"
        "GRID,98,,0,0,2 $ unused solver properties are optional\r\n"
        "ENDDATA\r\n");
    const auto mesh = readNastranMesh(path, no_progress);
    REQUIRE(mesh);
    CHECK(mesh->vertex_count_ == 7);
    CHECK(mesh->vertex_positions_ == std::vector<std::array<double, 3>> { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 0, 0, 2 } });
    CHECK(mesh->face_vertices_offset_ == std::vector<Index> { 0, 3, 7 });
    CHECK(mesh->face_vertices_ == std::vector<Index> { 0, 1, 2, 0, 1, 4, 3 });
    CHECK(mesh->solid_types_ == std::vector<unsigned char> { 10, 13 }); // VTK_TETRA、VTK_WEDGE
    CHECK(mesh->solid_vertices_offset_ == std::vector<Index> { 0, 4, 10 });
    CHECK(mesh->solid_vertices_ == std::vector<Index> { 3, 4, 5, 6, 0, 1, 2, 3, 4, 5 });
}

TEST_CASE("Nastran ignores commas in fixed-field sequence suffixes", "[NastranImport]")
{
    std::string deck = "BEGIN BULK\r\n";
    deck += fixedLine("GRID*", 16, { "9876543210", "", "1.25D+1", "-.75" }, "*P1");
    deck += fixedLine("*P1", 16, { "-25.-1" }, "X,Y");
    deck += fixedLine("GRID", 8, { "20", "", "1", "0", "0" }, "X,Y");
    deck += "GRID,30,,0,1,3.25\r\n";
    deck += "CTRIA3,30,1,9876543210,20,30\r\n";
    deck += "ENDDATA\r\n";

    const auto mesh = readNastranMesh(writeDeck(deck, ".bdf"), no_progress);
    REQUIRE(mesh);
    CHECK(mesh->vertex_positions_ == std::vector<std::array<double, 3>> { { 12.5, -0.75, -2.5 }, { 1, 0, 0 }, { 0, 1, 3.25 } });
    CHECK(mesh->face_vertices_ == std::vector<Index> { 0, 1, 2 });
}

TEST_CASE("Nastran joins unlabeled fixed continuation rows past sequence suffixes", "[NastranImport]")
{
    for (const char* suffix : { "00000012", "X,Y" }) {
        CAPTURE(suffix);
        std::string deck = fixedLine("GRID*", 16, { "10", "", "1.25", "-2.5" }, suffix);
        deck += fixedLine("*", 16, { "3.75" });
        deck += "GRID,20,,1,0,0\n"
                "GRID,30,,0,1,0\n"
                "CTRIA3,700,2,10,20,30\n";

        const auto mesh = readNastranMesh(writeDeck(deck), no_progress);
        REQUIRE(mesh);
        CHECK(mesh->vertex_positions_ == std::vector<std::array<double, 3>> { { 1.25, -2.5, 3.75 }, { 1, 0, 0 }, { 0, 1, 0 } });
        CHECK(mesh->face_vertices_ == std::vector<Index> { 0, 1, 2 });
    }
}

TEST_CASE("Nastran links nonadjacent named fixed continuation rows in either order", "[NastranImport]")
{
    const auto parent = fixedLine("CQUAD4*", 16, { "700", "2", "10", "20" }, "+P1");
    const auto first = fixedLine("*P1", 16, { "30", "40", "0.0", "0.0" }, "AP2");
    const auto second = fixedLine("*P2", 16, { "", "0", "1.0", "1.0" }, "BP3");
    const auto third = fixedLine("*P3", 16, { "1.0", "1.0" });
    constexpr auto first_grid = "GRID,10,,0,0,0\n";
    constexpr auto second_grid = "GRID,20,,1,0,0\n";

    std::string deck = "BEGIN BULK\n";
    SECTION("The named rows follow their parent")
    {
        deck += parent + first_grid + first + second_grid + second + third;
    }
    SECTION("The named rows precede their parent and appear out of link order")
    {
        deck += second + first_grid + first + second_grid + third + parent;
    }
    deck += "GRID,30,,1,1,0\n"
            "GRID,40,,0,1,0\n"
            "ENDDATA\n";

    const auto mesh = readNastranMesh(writeDeck(deck), no_progress);
    REQUIRE(mesh);
    CHECK(mesh->vertex_count_ == 4);
    CHECK(mesh->face_vertices_offset_ == std::vector<Index> { 0, 4 });
    CHECK(mesh->face_vertices_ == std::vector<Index> { 0, 1, 2, 3 });
}

TEST_CASE("Nastran ignores a sequence number that matches another card's continuation", "[NastranImport]")
{
    std::string deck = fixedLine("GRID", 8, { "20", "", "1", "0", "0" }, "00000012");
    deck += fixedLine("GRID*", 16, { "10", "", "1.25", "-2.5" }, "*0000012");
    deck += fixedLine("*0000012", 16, { "3.75" });
    deck += "GRID,30,,0,1,0\n"
            "CTRIA3,700,2,10,20,30\n";

    const auto mesh = readNastranMesh(writeDeck(deck), no_progress);
    REQUIRE(mesh);
    CHECK(mesh->vertex_positions_ == std::vector<std::array<double, 3>> { { 1, 0, 0 }, { 1.25, -2.5, 3.75 }, { 0, 1, 0 } });
    CHECK(mesh->face_vertices_ == std::vector<Index> { 1, 0, 2 });
}

TEST_CASE("Nastran rejects conflicting owners of a named continuation", "[NastranImport]")
{
    const auto short_grid = fixedLine("GRID", 8, { "10", "", "1", "2", "0" });
    const auto marked_grid = fixedLine("GRID", 8, { "10", "", "1", "2", "0" }, "+N7");
    const auto long_grid = fixedLine("GRID*", 16, { "20", "", "4", "5" }, "BN7");
    const auto child = fixedLine("*N7", 16, { "8" });
    const std::string remainder = "GRID,30,,0,1,0\n"
                                  "CTRIA3,700,2,10,20,30\n";

    const auto check_conflict = [&](const std::string& deck, std::string_view label) {
        const auto path = writeDeck(deck, ".bdf");
        const auto error = readError(path);
        CHECK(error.find("ambiguous continuation identifier " + std::string(label)) != std::string::npos);
        for (const char* line : { "1", "2" }) {
            CAPTURE(line);
            CHECK((error.find(path.filename().string() + ":" + line) != std::string::npos
                || error.find(std::string("line ") + line) != std::string::npos));
        }
    };

    SECTION("A unique owner receives the coordinate from the continuation")
    {
        const auto mesh = readNastranMesh(writeDeck(short_grid + long_grid + child + remainder), no_progress);
        REQUIRE(mesh);
        CHECK(mesh->vertex_positions_ == std::vector<std::array<double, 3>> { { 1, 2, 0 }, { 4, 5, 8 }, { 0, 1, 0 } });
    }
    SECTION("Marked and unmarked claims conflict in either order")
    {
        check_conflict(marked_grid + long_grid + child + remainder, "N7");
        check_conflict(long_grid + marked_grid + child + remainder, "N7");
    }
    SECTION("A numeric tail on a long GRID can be a continuation reference")
    {
        const auto competing_grid = fixedLine("GRID", 8, { "10", "", "1", "2", "0" }, "+0000012");
        const auto numeric_grid = fixedLine("GRID*", 16, { "20", "", "4", "5" }, "00000012");
        const auto numeric_child = fixedLine("*0000012", 16, { "8" });
        check_conflict(competing_grid + numeric_grid + numeric_child + remainder, "0000012");
    }
    SECTION("An adjacent long continuation conflicts with a remote marked claim")
    {
        const auto unmarked_grid = fixedLine("GRID*", 16, { "20", "", "4", "5" });
        check_conflict(marked_grid + unmarked_grid + child + remainder, "N7");
    }
}

TEST_CASE("Nastran rejects malformed named fixed continuation links", "[NastranImport]")
{
    const auto parent = fixedLine("GRID*", 16, { "10", "", "1", "2" }, "+P1");
    const auto child = fixedLine("*P1", 16, { "3" });
    const std::string rest = "GRID,20,,0,0,0\n"
                             "GRID,30,,0,1,0\n"
                             "CTRIA3,700,2,10,20,30\n";

    SECTION("Duplicate named row")
    {
        const auto path = writeDeck(parent + rest + child + child);
        checkErrorLocation(readError(path), path, 6, "P1");
    }
    SECTION("Named row without a parent")
    {
        const auto path = writeDeck(std::string(triangle_deck) + child);
        checkErrorLocation(readError(path), path, 5, "P1");
    }
    SECTION("Continuation links back to itself")
    {
        const auto path = writeDeck(parent + rest + fixedLine("*P1", 16, { "3" }, "AP1"));
        checkErrorLocation(readError(path), path, 5, "P1");
    }
}

TEST_CASE("Nastran preserves a blank fourth slot on a large free-field row", "[NastranImport]")
{
    for (const char* header : { "GRID*,10,,1.\n", "GRID*,10,,1.,\n", "GRID*,10,,1.,,\n" }) {
        CAPTURE(header);
        std::string deck = header;
        deck += "*,2.\n"
                "GRID,20,,1,0,0\n"
                "GRID,30,,0,1,0\n"
                "CTRIA3,700,2,10,20,30\n";

        const auto mesh = readNastranMesh(writeDeck(deck), no_progress);
        REQUIRE(mesh);
        REQUIRE(mesh->vertex_positions_.size() == 3);
        CHECK(mesh->vertex_positions_.front() == std::array<double, 3> { 1, 0, 2 });
    }
}

TEST_CASE("Nastran does not move a continuation value into an earlier short free-field slot", "[NastranImport]")
{
    const auto path = writeDeck(
        "CTRIA3,700,2,10,20,\n"
        "+,30\n"
        "GRID,10,,0,0,0\n"
        "GRID,20,,1,0,0\n"
        "GRID,30,,0,1,0\n");
    const auto error = readError(path);
    checkErrorLocation(error, path, 1, "CTRIA3");
    CHECK(error.find("700") != std::string::npos);
}

TEST_CASE("Nastran rejects a mismatched fixed continuation label", "[NastranImport]")
{
    std::string deck = "BEGIN BULK\r\n";
    deck += fixedLine("GRID*", 16, { "10", "", "1", "0" }, "*P1");
    deck += fixedLine("*P2", 16, { "0" });
    const auto path = writeDeck(deck);
    const auto error = readError(path);
    checkErrorLocation(error, path, 3, "GRID");
    CHECK(error.find("line 2") != std::string::npos);
}

TEST_CASE("Nastran accepts an omitted empty large fixed-field half without a continuation marker", "[NastranImport]")
{
    const std::string remaining_cards = "GRID,20,,0,0,0\n"
                                        "GRID,30,,0,1,0\n"
                                        "CTRIA3,700,2,10,20,30\n";

    SECTION("The empty last half is optional")
    {
        const auto path = writeDeck(fixedLine("GRID*", 16, { "10", "", "1", "2" }) + remaining_cards);
        const auto mesh = readNastranMesh(path, no_progress);
        REQUIRE(mesh);
        REQUIRE(mesh->vertex_positions_.size() == 3);
        CHECK(mesh->vertex_positions_.front() == std::array<double, 3> { 1, 2, 0 });
    }

    SECTION("A declared continuation must be present")
    {
        const auto path = writeDeck(fixedLine("GRID*", 16, { "10", "", "1", "2" }, "*P1") + remaining_cards);
        checkErrorLocation(readError(path), path, 1, "GRID");
    }
}

TEST_CASE("Nastran reports the card and source line on a bad mesh deck", "[NastranImport]")
{
    struct BadDeck {
        std::string_view content;
        std::size_t line;
        std::string_view card;
        std::string_view id;
    };
    const BadDeck fixtures[] {
        { "CTRIA3,700,2,10,20,30\nGRID,10,,0,0,0\nGRID,10,,1,0,0\nGRID,20,,1,0,0\nGRID,30,,0,1,0\n", 3, "GRID", "10" },
        { "CTRIA3,700,2,10,20,30\nCQUAD4,700,2,10,20,30,40\nGRID,10,,0,0,0\nGRID,20,,1,0,0\nGRID,30,,1,1,0\nGRID,40,,0,1,0\n", 2, "CQUAD4", "700" },
        { "CTRIA3,700,2,10,20,931\nGRID,10,,0,0,0\nGRID,20,,1,0,0\n", 1, "CTRIA3", "931" },
        { "CTRIA3,700,2,10,20,20\nGRID,10,,0,0,0\nGRID,20,,1,0,0\n", 1, "CTRIA3", "700" },
        { "GRID,371,,1.0oops,0,0\n", 1, "GRID", "371" },
        { "GRID,371,,1.D+999,0,0\n", 1, "GRID", "371" },
        { "CTRIA3,700,2,371,20,30\nGRID,371,17,0,0,0\nGRID,20,,1,0,0\nGRID,30,,0,1,0\n", 2, "GRID", "371" },
        { "CTRIA6,700,2,10,20,30,40,50,60\n", 1, "CTRIA6", "700" },
        { "CTETRA,700,2,10,20,30,40,50\n", 1, "CTETRA", "700" },
        { "BEGIN BULK\nINCLUDE 'mesh-2.nas'\nENDDATA\n", 2, "INCLUDE", {} },
        { "APP DISP\nINCLUDE 'controls.nas'\nSOL 101\nCEND\nBEGIN BULK\nENDDATA\n", 2, "INCLUDE", {} },
    };
    for (const auto& fixture : fixtures) {
        CAPTURE(std::string(fixture.content));
        const auto path = writeDeck(fixture.content);
        const auto error = readError(path);
        checkErrorLocation(error, path, fixture.line, fixture.card);
        if (!fixture.id.empty()) {
            CHECK(error.find(fixture.id) != std::string::npos);
        }
    }
}

TEST_CASE("Nastran rejects superelement sections instead of importing them as ordinary bulk data", "[NastranImport]")
{
    for (const char* section : { "BEGIN BULK SUPER = 11\n", "BEGIN SUPER = 11\n" }) {
        CAPTURE(section);
        const auto path = writeDeck(std::string("SOL 101\nCEND\n") + section + std::string(triangle_deck) + "ENDDATA\n");
        checkErrorLocation(readError(path), path, 3, "BEGIN");
    }
}

TEST_CASE("Nastran applies GRDSET only to a blank GRID input coordinate system", "[NastranImport]")
{
    SECTION("An explicit basic coordinate system overrides a later default")
    {
        const auto path = writeDeck(
            "CTRIA3,700,2,10,20,30\n"
            "GRID,10,0,0,0,0,19\n"
            "GRID,20,0,1,0,0\n"
            "GRID,30,0,0,1,0\n"
            "GRDSET,,17\n");
        const auto mesh = readNastranMesh(path, no_progress);
        REQUIRE(mesh);
        CHECK(mesh->face_vertices_ == std::vector<Index> { 0, 1, 2 });
    }

    SECTION("A later non-basic default applies to a blank input coordinate system")
    {
        const auto path = writeDeck(std::string(triangle_deck) + "GRDSET,,17\n");
        const auto error = readError(path);
        checkErrorLocation(error, path, 2, "GRID");
        CHECK(error.find("17") != std::string::npos);
    }
}

TEST_CASE("Nastran import executes through ModelIOSystem with a UTF-8 filename", "[NastranImport]")
{
    auto path = core::TempFile::instance().path();
    path.replace_extension(fs::path(u8".三角网格.BDF"));
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output);
        output << triangle_deck;
        REQUIRE(output);
    }

    ImportedModelObserver observer;
    ModelLayer model(&observer);
    ModelIOSystem system(model);

    const HandlerMetaData metadata { "Nastran 网格", { "nas", "bdf" }, true };
    auto handler = std::make_unique<NastranImportHandler>();
    REQUIRE(system.registerHandler(metadata, ModelIOSystem::SystemHandlerPtr { handler.release() }));

    const auto formats = system.registeredFileTypeInfos();
    REQUIRE(formats.size() == 1);
    CHECK(formats.front()->name == metadata.file_type);
    CHECK(formats.front()->extensions == std::vector<std::string> { "nas", "bdf" });
    CHECK(formats.front()->read_only);

    REQUIRE(system.read(path, metadata.file_type, {}));

    const auto* model_data = model.modelById(observer.added);
    REQUIRE(model_data);
    const auto utf8_name = path.filename().u8string();
    CHECK(model_data->model_name_ == std::string(utf8_name.begin(), utf8_name.end()));
    REQUIRE(model_data->componentIds().size() == 1);
    const auto* component = model.findComponent(model_data->componentIds().front());
    REQUIRE(component);
    REQUIRE(component->mesh);
    CHECK(component->mesh->vertex_count_ == 3);
    CHECK(component->mesh->face_vertices_ == std::vector<Index> { 0, 1, 2 });
}
