# Topology-independent merge engine + Xbox report packer.
# Pure C, no ESP-IDF APIs — also compiled standalone by firmware/test/.
idf_component_register(
    SRCS
        "merge_engine.c"
        "xbox_report.c"
    INCLUDE_DIRS
        "include"
)
