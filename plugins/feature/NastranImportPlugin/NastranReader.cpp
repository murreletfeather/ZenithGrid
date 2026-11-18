#include "NastranReader.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "MeshData.h"

namespace systems::feature {
namespace {

    using ExternalId = std::int64_t;
    constexpr std::size_t kProgressInterval = 2048;
    constexpr std::size_t kMaxIndex = (std::numeric_limits<Index>::max)();
    constexpr std::size_t kFreeFieldPrefixWidth = 10;
    constexpr double kSectionScanProgress = 0.1;
    constexpr double kContinuationScanProgress = 0.2;
    constexpr double kReadingProgress = 0.7;

    // MeshData 使用 VTK 的体单元类型编号。
    constexpr unsigned char kVtkTetra = 10;
    constexpr unsigned char kVtkWedge = 13;

    struct PhysicalLine {
        std::string text;
        std::size_t number;
    };

    struct ContinuationRun {
        std::vector<PhysicalLine> rows;
        std::size_t following_named_line = 0;
        std::size_t owner_line = 0;
    };

    struct CardPrefix {
        std::string_view first_field;
        std::size_t comma;
    };

    struct CardRow {
        std::string first_field;
        std::vector<std::string> fields;
        std::string continuation;
        std::size_t payload_slots;
        bool has_marker = false;
    };

    struct BulkCard {
        std::string name;
        std::vector<std::string> fields;
        std::size_t line;
    };

    struct GridRecord {
        ExternalId id;
        std::optional<ExternalId> cp;
        std::array<double, 3> position;
        std::size_t line;
    };

    struct ElementType {
        std::string_view name;
        std::size_t corners;
        std::size_t max_node_fields;
        unsigned char vtk_type;
    };

    constexpr std::array kElementTypes {
        ElementType { "CTRIA3", 3, 3, 0 },
        ElementType { "CQUAD4", 4, 4, 0 },
        ElementType { "CTETRA", 4, 10, kVtkTetra },
        ElementType { "CPENTA", 6, 15, kVtkWedge },
    };

    const ElementType* findElementType(std::string_view name)
    {
        const auto found = std::find_if(kElementTypes.begin(), kElementTypes.end(),
            [name](const ElementType& type) { return type.name == name; });
        return found == kElementTypes.end() ? nullptr : &*found;
    }

    struct ElementRecord {
        const ElementType* type;
        ExternalId id;
        std::array<ExternalId, 6> corners;
        std::size_t line;
    };

    std::string_view trim(std::string_view input)
    {
        const auto begin = input.find_first_not_of(" \t\r");
        if (begin == std::string_view::npos)
            return {};
        return input.substr(begin, input.find_last_not_of(" \t\r") - begin + 1);
    }

    std::string upper(std::string_view input)
    {
        std::string result(input);
        for (char& ch : result) {
            if (ch >= 'a' && ch <= 'z')
                ch -= 'a' - 'A';
        }
        return result;
    }

    std::string pathText(const std::filesystem::path& path)
    {
        const auto utf8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }

    [[noreturn]] void fail(const std::filesystem::path& path, std::size_t line, std::string_view card,
        std::string_view message)
    {
        std::string error = pathText(path);
        if (line != 0)
            error += ":" + std::to_string(line);
        if (!card.empty())
            error += " [" + std::string(card) + "]";
        throw std::runtime_error(error + " " + std::string(message));
    }

    std::string_view field(const BulkCard& card, std::size_t index)
    {
        return index < card.fields.size() ? card.fields[index] : std::string_view {};
    }

    bool hasContinuationMarker(std::string_view field)
    {
        return field.starts_with('+') || field.starts_with('*');
    }

    bool isContinuation(std::string_view first)
    {
        return first.empty() || hasContinuationMarker(first);
    }

    std::string_view continuationLabel(std::string_view field)
    {
        if (hasContinuationMarker(field))
            field.remove_prefix(1);
        return trim(field);
    }

    CardPrefix splitCardPrefix(std::string_view line)
    {
        // 定宽注释或续行标记里的逗号不改变数据字段布局。
        const auto comma = line.substr(0, kFreeFieldPrefixWidth).find(',');
        return { trim(line.substr(0, comma == std::string_view::npos ? 8 : comma)), comma };
    }

    std::string_view fixedContinuationField(std::string_view line)
    {
        return line.size() > 72 ? line.substr(72, 8) : std::string_view {};
    }

    CardRow splitRow(std::string_view line)
    {
        CardRow row;
        const auto [first, comma] = splitCardPrefix(line);
        row.first_field = upper(first);
        const bool large = row.first_field.ends_with('*') || row.first_field.starts_with('*');
        row.payload_slots = large ? 4 : 8;
        row.fields.reserve(row.payload_slots);
        if (comma != std::string_view::npos) {
            line.remove_prefix(comma + 1);
            for (;;) {
                const auto next_comma = line.find(',');
                row.fields.emplace_back(trim(line.substr(0, next_comma)));
                if (next_comma == std::string_view::npos)
                    break;
                line.remove_prefix(next_comma + 1);
            }
            if (row.fields.size() == row.payload_slots + 1 && isContinuation(row.fields.back())) {
                row.has_marker = !row.fields.back().empty();
                row.continuation = upper(continuationLabel(row.fields.back()));
                row.fields.pop_back();
            }
            return row;
        }

        // 固定字段的空槽位影响续行中的字段下标。
        const std::size_t width = large ? 16 : 8;
        for (std::size_t start = 8; start < 72; start += width) {
            row.fields.emplace_back(start < line.size() ? trim(line.substr(start, width)) : std::string_view {});
        }
        const auto trailer = fixedContinuationField(line);
        if (!trailer.empty()) {
            row.has_marker = hasContinuationMarker(trailer);
            // 续行标签按第 74–80 列匹配。第 73 列可以是任意字符。
            row.continuation = upper(trim(trailer.substr(1)));
        }
        return row;
    }

    std::string_view firstWord(std::string_view line)
    {
        line = trim(line);
        return line.substr(0, line.find_first_of(" \t,="));
    }

    bool isEndData(std::string_view word)
    {
        return word == "ENDDATA" || word == "ENDATA";
    }

    class CardReader {
    public:
        CardReader(const std::filesystem::path& path, const systems::job::ProgressFn& report)
            : path_(path)
            , report_(report)
            , input_(path, std::ios::binary)
        {
            if (!input_)
                fail(path_, 0, {}, "cannot open Nastran input");
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (!error)
                size_ = size;
            locateBulkSection();
            indexContinuations();
        }

        std::optional<BulkCard> next()
        {
            while (auto physical = takeLine()) {
                if (isIndexedLine(physical->number))
                    continue;
                const auto word = readKeyword(*physical);
                if (isEndData(word))
                    break;
                if (word == "BEGIN")
                    fail(path_, physical->number, word, "unsupported Nastran section");
                return joinCard(std::move(*physical));
            }

            for (const auto& run : continuations_) {
                if (!run.owner_line)
                    fail(path_, run.rows.front().number, {}, "orphan continuation identifier " + upper(continuationLabel(splitCardPrefix(run.rows.front().text).first_field)));
            }
            return std::nullopt;
        }

    private:
        std::string readKeyword(const PhysicalLine& line) const
        {
            auto word = upper(firstWord(line.text));
            if (word == "INCLUDE")
                fail(path_, line.number, word, "external INCLUDE files are unsupported");
            return word;
        }

        void locateBulkSection()
        {
            bool has_control_section = false;
            while (auto physical = takeLine(0.0, kSectionScanProgress)) {
                const auto word = readKeyword(*physical);
                if (isEndData(word))
                    break;
                if (word == "BEGIN") {
                    auto tail = trim(trim(physical->text).substr(word.size()));
                    if (tail.starts_with(','))
                        tail.remove_prefix(1);
                    tail = trim(tail);
                    const auto section = firstWord(tail);
                    const auto qualifiers = tail.substr(section.size());
                    if (upper(section) != "BULK" || qualifiers.find_first_not_of(" \t\r,") != std::string_view::npos)
                        fail(path_, physical->number, word, "unsupported Nastran section");
                    return;
                }
                has_control_section = has_control_section || word == "SOL" || word == "CEND";
                // 已进入有网格数据的 punch，不再把其后出现的 BEGIN 当成文件头。
                auto name = upper(splitCardPrefix(physical->text).first_field);
                if (name.ends_with('*'))
                    name.pop_back();
                if (name == "GRID" || name == "GRDSET" || findElementType(name))
                    break;
            }
            if (has_control_section)
                fail(path_, line_number_, {}, "missing BEGIN BULK");

            // 没有段标记的 punch 文件从首行读取，恢复原始位置和行号。
            rewind(0, 0, 0);
        }

        void rewind(std::streampos position, std::size_t line, std::uintmax_t bytes)
        {
            input_.clear();
            input_.seekg(position);
            if (!input_)
                fail(path_, line_number_, {}, "cannot rewind Nastran input");
            line_number_ = line;
            bytes_ = bytes;
        }

        void reserveMarkedContinuation(std::string_view line, const CardPrefix& prefix)
        {
            // 即使普通列尾序号恰好相同，也优先保留明确声明的续行目标。
            std::string label;
            if (prefix.comma == std::string_view::npos) {
                const auto trailer = fixedContinuationField(line);
                if (hasContinuationMarker(trailer))
                    label = upper(continuationLabel(trailer));
            } else if (hasContinuationMarker(trim(line.substr(line.rfind(',') + 1)))) {
                label = splitRow(line).continuation;
            }
            if (!label.empty())
                marked_continuations_.insert(std::move(label));
        }

        void indexContinuations()
        {
            input_.clear();
            const auto position = input_.tellg();
            if (!input_)
                fail(path_, line_number_, {}, "cannot record Nastran input position");
            const auto start_line = line_number_;
            const auto start_bytes = bytes_;

            // 保留命名续行及其紧随的自动续行，普通卡片仍按原顺序流式解析。
            bool collecting = false;
            while (auto line = takeLine(kSectionScanProgress, kContinuationScanProgress)) {
                if (isEndData(upper(firstWord(line->text))))
                    break;
                const auto prefix = splitCardPrefix(line->text);
                reserveMarkedContinuation(line->text, prefix);
                const auto first = prefix.first_field;
                if (!isContinuation(first)) {
                    collecting = false;
                    continue;
                }
                auto label = upper(continuationLabel(first));
                if (label.empty()) {
                    if (collecting)
                        continuations_.back().rows.push_back(std::move(*line));
                    continue;
                }
                if (!continuation_indices_.emplace(label, continuations_.size()).second)
                    fail(path_, line->number, {}, "duplicate continuation identifier " + label);
                if (collecting)
                    continuations_.back().following_named_line = line->number;
                continuations_.emplace_back();
                continuations_.back().rows.push_back(std::move(*line));
                collecting = true;
            }
            rewind(position, start_line, start_bytes);
        }

        bool isIndexedLine(std::size_t line)
        {
            while (indexed_run_ < continuations_.size() && continuations_[indexed_run_].rows.back().number < line)
                ++indexed_run_;
            return indexed_run_ < continuations_.size() && continuations_[indexed_run_].rows.front().number <= line;
        }

        void reportProgress(double start, double end) const
        {
            const double fraction = size_ == 0 ? 0.0 : static_cast<double>(bytes_) / size_;
            report_(start + (end - start) * (std::min)(1.0, fraction), "读取 Nastran 网格");
        }

        std::optional<PhysicalLine> takeLine(double progress_start = kContinuationScanProgress, double progress_end = kReadingProgress)
        {
            if (pending_)
                return std::exchange(pending_, std::nullopt);

            std::string line;
            while (std::getline(input_, line)) {
                ++line_number_;
                bytes_ += line.size() + 1;
                if (line_number_ % kProgressInterval == 0)
                    reportProgress(progress_start, progress_end);

                if (line.find('\0') != std::string::npos)
                    fail(path_, line_number_, {}, "expected Nastran text input");
                if (line_number_ == 1 && line.starts_with("\xEF\xBB\xBF"))
                    line.erase(0, 3);
                if (const auto comment = line.find('$'); comment != std::string::npos)
                    line.resize(comment);
                const auto nonblank = trim(line);
                if (!nonblank.empty() && !nonblank.starts_with('#') && !nonblank.starts_with("//"))
                    return PhysicalLine { std::move(line), line_number_ };
            }
            if (input_.bad() || !input_.eof())
                fail(path_, line_number_, {}, "error reading Nastran input");
            return std::nullopt;
        }

        BulkCard joinCard(PhysicalLine header)
        {
            auto row = splitRow(header.text);
            if (isContinuation(row.first_field))
                fail(path_, header.number, {}, "orphan continuation");
            if (row.first_field.ends_with('*'))
                row.first_field.pop_back();
            BulkCard card { std::move(row.first_field), {}, header.number };
            const ContinuationRun* run = nullptr;
            std::size_t run_position = 0;
            std::size_t current_line = header.number;
            std::size_t joined = 0;

            for (;;) {
                const PhysicalLine* adjacent = nullptr;
                if (run) {
                    if (run_position + 1 < run->rows.size())
                        adjacent = &run->rows[run_position + 1];
                } else {
                    pending_ = takeLine();
                    if (pending_)
                        adjacent = &*pending_;
                }

                const auto first = adjacent ? splitCardPrefix(adjacent->text).first_field : std::string_view {};
                const bool adjacent_continuation = adjacent && isContinuation(first);
                const bool automatic = adjacent_continuation && continuationLabel(first).empty();
                auto named = !row.continuation.empty() && (row.has_marker || !automatic)
                    ? continuation_indices_.find(row.continuation)
                    : continuation_indices_.end();
                // 普通尾栏不能抢占显式标签，但指回本卡已使用的标签仍是循环引用。
                if (named != continuation_indices_.end() && !row.has_marker
                    && marked_continuations_.contains(row.continuation)
                    && continuations_[named->second].owner_line != card.line)
                    named = continuation_indices_.end();

                const PhysicalLine* next = nullptr;
                if (named != continuation_indices_.end()) {
                    if (run && automatic)
                        fail(path_, adjacent->number, card.name, "orphan automatic continuation after explicit identifier " + row.continuation);
                    auto& target = continuations_[named->second];
                    if (target.owner_line) {
                        const auto reason = target.owner_line == card.line ? "cyclic continuation identifier " : "reused continuation identifier ";
                        fail(path_, current_line, card.name, reason + row.continuation);
                    }
                    target.owner_line = card.line;
                    run = &target;
                    run_position = 0;
                    next = &target.rows.front();
                } else if (row.has_marker && (!automatic || !row.continuation.empty())) {
                    const auto mismatched_line = adjacent_continuation ? adjacent->number : (run ? run->following_named_line : 0);
                    if (mismatched_line)
                        fail(path_, mismatched_line, card.name, "mismatched continuation identifier (card begins at line " + std::to_string(card.line) + ")");
                    fail(path_, current_line, card.name, "missing continuation");
                } else if (automatic) {
                    next = adjacent;
                    if (run)
                        ++run_position;
                }

                if (next) {
                    // 自由格式也按物理行的槽位接续。省略的尾部字段保留为空，续行不能前移。
                    if (row.fields.size() > row.payload_slots)
                        fail(path_, card.line, card.name, "too many data fields before continuation at line " + std::to_string(next->number));
                    row.fields.resize(row.payload_slots);
                }
                if (!joined) {
                    card.fields = std::move(row.fields);
                } else {
                    card.fields.insert(card.fields.end(), std::make_move_iterator(row.fields.begin()),
                        std::make_move_iterator(row.fields.end()));
                }
                if (!next)
                    break;
                current_line = next->number;
                row = splitRow(next->text);
                if (!run)
                    pending_.reset();
                if (++joined % kProgressInterval == 0)
                    reportProgress(kContinuationScanProgress, kReadingProgress);
            }
            return card;
        }

        const std::filesystem::path& path_;
        const systems::job::ProgressFn& report_;
        std::ifstream input_;
        std::uintmax_t size_ = 0;
        std::uintmax_t bytes_ = 0;
        std::size_t line_number_ = 0;
        std::optional<PhysicalLine> pending_;
        std::vector<ContinuationRun> continuations_;
        std::unordered_map<std::string, std::size_t> continuation_indices_;
        std::unordered_set<std::string> marked_continuations_;
        std::size_t indexed_run_ = 0;
    };

    std::optional<ExternalId> tryReadInteger(std::string_view value)
    {
        if (value.starts_with('+')) {
            value.remove_prefix(1);
            if (value.starts_with('-'))
                return std::nullopt;
        }
        if (value.empty())
            return std::nullopt;
        ExternalId number = 0;
        const auto* end = value.data() + value.size();
        const auto result = std::from_chars(value.data(), end, number);
        if (result.ec != std::errc {} || result.ptr != end)
            return std::nullopt;
        return number;
    }

    ExternalId readId(std::string_view value, ExternalId minimum, const BulkCard& card,
        const std::filesystem::path& path, std::string_view field_name, ExternalId record_id = 0)
    {
        const auto id = tryReadInteger(value);
        if (!id || *id < minimum) {
            std::string message = "invalid " + std::string(field_name) + ": " + std::string(value);
            if (record_id != 0)
                message += " (record ID " + std::to_string(record_id) + ")";
            fail(path, card.line, card.name, message);
        }
        return *id;
    }

    bool tryReadDouble(std::string_view input, double& output)
    {
        std::istringstream stream { std::string(input) };
        stream.imbue(std::locale::classic());
        stream >> std::noskipws >> output;
        return stream && stream.eof() && std::isfinite(output);
    }

    double readCoordinate(std::string_view value, const BulkCard& card, ExternalId id,
        const std::filesystem::path& path)
    {
        if (value.empty())
            return 0.0;
        double coordinate = 0.0;
        if (tryReadDouble(value, coordinate))
            return coordinate;

        // Nastran 同时允许 Fortran 指数和省略 E 的紧凑指数。
        std::string normalized(value);
        if (const auto marker = normalized.find_first_of("Dd"); marker != std::string::npos) {
            normalized[marker] = 'E';
        } else if (normalized.find_first_of("Ee") == std::string::npos) {
            const auto sign = normalized.find_last_of("+-");
            if (sign != std::string::npos && sign != 0)
                normalized.insert(sign, 1, 'E');
        }
        if (tryReadDouble(normalized, coordinate))
            return coordinate;
        fail(path, card.line, card.name, "invalid coordinate for GRID " + std::to_string(id) + ": " + std::string(value));
    }

    std::optional<ExternalId> readCp(const BulkCard& card, const std::filesystem::path& path, ExternalId grid_id = 0)
    {
        const auto value = field(card, 1);
        if (value.empty())
            return std::nullopt;
        return readId(value, 0, card, path, "CP", grid_id);
    }

    bool isAnalysisCard(std::string_view name)
    {
        constexpr std::string_view ignored[] {
            "PSHELL",
            "PSOLID",
            "PLSOLID",
            "PCOMP",
            "PCOMPG",
            "PCOMPP",
            "PSHEAR",
            "PROD",
            "PTUBE",
            "PBAR",
            "PBARL",
            "PBEAM",
            "PBEAML",
            "PBUSH",
            "PELAS",
            "PGAP",
            "LOAD",
            "DLOAD",
            "GRAV",
            "TEMP",
            "TEMPD",
            "SPC",
            "SPC1",
            "SPCADD",
            "SPCD",
            "MPC",
            "MPCADD",
            "RBE1",
            "RBE2",
            "RBE3",
            "RBAR",
            "SUPORT",
            "SUPORT1",
            "PARAM",
            "EIGRL",
            "FORCE",
            "FORCE1",
            "FORCE2",
            "MOMENT",
            "MOMENT1",
            "MOMENT2",
            "PLOAD",
            "PLOAD1",
            "PLOAD2",
            "PLOAD4",
            "SET1",
            "SET3",
            "ASET",
            "ASET1",
            "CORD1R",
            "CORD1C",
            "CORD1S",
            "CORD2R",
            "CORD2C",
            "CORD2S",
        };
        return name.starts_with("MAT") || std::find(std::begin(ignored), std::end(ignored), name) != std::end(ignored);
    }

    ElementRecord readElement(const BulkCard& card, const ElementType& type, const std::filesystem::path& path)
    {
        const auto eid = readId(field(card, 0), 1, card, path, "EID");
        ElementRecord element { &type, eid, {}, card.line };
        for (std::size_t i = 0; i < type.corners; ++i) {
            const auto value = field(card, 2 + i);
            element.corners[i] = readId(value, 1, card, path, "corner GRID ID", eid);
        }
        for (std::size_t i = type.corners; i < type.max_node_fields; ++i) {
            const auto value = field(card, 2 + i);
            if (!value.empty() && tryReadInteger(value) != 0)
                fail(path, card.line, card.name, "higher-order or invalid node for EID " + std::to_string(eid));
        }
        if (type.vtk_type) {
            for (std::size_t i = 2 + type.max_node_fields; i < card.fields.size(); ++i) {
                if (!card.fields[i].empty())
                    fail(path, card.line, card.name, "unsupported solid element fields for EID " + std::to_string(eid));
            }
        }
        return element;
    }

    std::unique_ptr<MeshData> assembleMesh(const std::filesystem::path& path, const std::vector<GridRecord>& grids,
        const std::vector<ElementRecord>& elements, ExternalId default_cp, const systems::job::ProgressFn& report)
    {
        if (grids.empty() || elements.empty())
            fail(path, 0, {}, "no supported Nastran mesh cells and nodes found");
        if (grids.size() > kMaxIndex)
            fail(path, 0, "GRID", "vertex count exceeds local index range");

        auto mesh = std::make_unique<MeshData>();
        mesh->face_vertices_offset_.push_back(0);
        mesh->vertex_positions_.reserve(grids.size());
        std::unordered_map<ExternalId, Index> local_ids;
        local_ids.reserve(grids.size());
        for (const auto& grid : grids) {
            const auto cp = grid.cp.value_or(default_cp);
            if (cp != 0)
                fail(path, grid.line, "GRID", "unsupported CP " + std::to_string(cp) + " for GRID " + std::to_string(grid.id));
            const auto index = mesh->vertex_positions_.size();
            if (!local_ids.emplace(grid.id, static_cast<Index>(index)).second)
                fail(path, grid.line, "GRID", "duplicate GRID ID " + std::to_string(grid.id));
            mesh->vertex_positions_.push_back(grid.position);
            if (index % kProgressInterval == 0)
                report(0.7 + 0.1 * static_cast<double>(index) / grids.size(), "校验 Nastran 节点");
        }
        mesh->vertex_count_ = static_cast<Index>(mesh->vertex_positions_.size());

        std::unordered_set<ExternalId> element_ids;
        element_ids.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& element = elements[i];
            const auto& type = *element.type;
            if (!element_ids.insert(element.id).second)
                fail(path, element.line, type.name, "duplicate EID " + std::to_string(element.id));

            auto& vertices = type.vtk_type ? mesh->solid_vertices_ : mesh->face_vertices_;
            auto& offsets = type.vtk_type ? mesh->solid_vertices_offset_ : mesh->face_vertices_offset_;
            // VTK 转换时会将面、体单元的连接数组按同一索引类型合并。
            const auto connectivity_size = mesh->solid_vertices_.size() + mesh->face_vertices_.size();
            if (type.corners > kMaxIndex - connectivity_size)
                fail(path, element.line, type.name, "connectivity exceeds local index range");

            // CPENTA 与项目三棱柱形函数使用相同的角点顺序。
            for (std::size_t corner = 0; corner < type.corners; ++corner) {
                const auto id = element.corners[corner];
                if (std::find(element.corners.begin(), element.corners.begin() + corner, id) != element.corners.begin() + corner)
                    fail(path, element.line, type.name, "repeated corner GRID " + std::to_string(id) + " for EID " + std::to_string(element.id));
                const auto found = local_ids.find(id);
                if (found == local_ids.end())
                    fail(path, element.line, type.name, "missing GRID " + std::to_string(id) + " for EID " + std::to_string(element.id));
                vertices.push_back(found->second);
            }
            offsets.push_back(static_cast<Index>(vertices.size()));
            if (type.vtk_type) {
                mesh->solid_types_.push_back(type.vtk_type);
                mesh->solid_faces_offset_.push_back(0);
            }
            if (i % kProgressInterval == 0)
                report(0.8 + 0.2 * static_cast<double>(i) / elements.size(), "组装 Nastran 网格");
        }
        return mesh;
    }

}

std::unique_ptr<MeshData> readNastranMesh(const std::filesystem::path& path, const systems::job::ProgressFn& report)
{
    report(0.0, "读取 Nastran 网格");
    CardReader reader(path, report);
    std::vector<GridRecord> grids;
    std::vector<ElementRecord> elements;
    std::optional<ExternalId> default_cp;

    while (const auto card = reader.next()) {
        if (card->name == "GRID") {
            const auto id = readId(field(*card, 0), 1, *card, path, "GRID ID");
            const auto cp = readCp(*card, path, id);
            std::array<double, 3> position;
            for (std::size_t axis = 0; axis < position.size(); ++axis)
                position[axis] = readCoordinate(field(*card, axis + 2), *card, id, path);
            grids.push_back(GridRecord { id, cp, position, card->line });
        } else if (card->name == "GRDSET") {
            if (default_cp)
                fail(path, card->line, card->name, "duplicate GRDSET");
            default_cp = readCp(*card, path).value_or(0);
        } else if (const auto* type = findElementType(card->name)) {
            elements.push_back(readElement(*card, *type, path));
        } else if (!isAnalysisCard(card->name)) {
            fail(path, card->line, card->name, "unsupported Nastran card or topology, ID " + std::string(field(*card, 0)));
        }
    }

    auto mesh = assembleMesh(path, grids, elements, default_cp.value_or(0), report);
    report(1.0, "已读取 Nastran 网格");
    return mesh;
}

}
