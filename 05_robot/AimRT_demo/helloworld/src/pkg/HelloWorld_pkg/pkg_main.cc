#include <cstring>

#include "HelloWorld_module/HelloWorld_module.h"
#include "aimrt_pkg_c_interface/pkg_macro.h"

static std::tuple<std::string_view, std::function<aimrt::ModuleBase*()>>
    aimrt_module_register_array[]{
        {"HelloworldModule",
         []() -> aimrt::ModuleBase* {
           return new helloworld::HelloWorld_module::HelloworldModule();
         }},
    };

AIMRT_PKG_MAIN(aimrt_module_register_array)
