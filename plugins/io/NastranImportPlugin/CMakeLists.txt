precess_add_io_plugin(NastranImportPlugin
	SOURCES "NastranImportHandler.cpp" "NastranReader.cpp"
	PLUGIN_H "NastranImportPlugin.h"
)
add_subdirectory(test)
