import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get


required_conan_version = ">=2.0"


class PlcopenConan(ConanFile):
    name = "plcopen"
    description = "C++17 PLCopen motion-control kernel and IEC 61131-3 ST runtime"
    license = "Apache-2.0"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/lusipad/plcopen"
    topics = ("motion-control", "plcopen", "robotics", "real-time", "iec-61131-3")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def layout(self):
        cmake_layout(self, src_folder="src")

    def package_id(self):
        self.info.clear()

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, "17")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.cache_variables["BUILD_TESTING"] = False
        toolchain.cache_variables["PLCOPEN_BUILD_DEMOS"] = False
        toolchain.cache_variables["PLCOPEN_BUILD_DOCS"] = False
        toolchain.cache_variables["PLCOPEN_BUILD_LEGACY"] = False
        toolchain.cache_variables["PLCOPEN_BUILD_PYTHON_BINDINGS"] = False
        toolchain.cache_variables["PLCOPEN_BUILD_TESTS"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", self.source_folder,
             os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.includedirs = ["include/plcopen"]
        self.cpp_info.set_property("cmake_file_name", "plcopen")
        self.cpp_info.set_property("cmake_target_name", "plcopen::plcopen")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
