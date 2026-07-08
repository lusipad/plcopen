from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy
import os


class PlcopenConan(ConanFile):
    name = "plcopen"
    version = "1.0.0-alpha"
    license = "Apache-2.0"
    url = "https://github.com/lusipad/plcopen"
    description = "PLCopen motion-control kernel — C++17 header-only library"
    topics = ("motion-control", "plcopen", "robotics", "real-time")
    package_type = "header-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    no_copy_source = True

    def layout(self):
        cmake_layout(self)

    def package_id(self):
        self.info.clear()

    def export_sources(self):
        copy(self, "CMakeLists.txt", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "core/*", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "cmake/*", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "LICENSE", src=self.recipe_folder, dst=self.export_sources_folder)

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={
            "PLCOPEN_BUILD_TESTS": "OFF",
            "PLCOPEN_BUILD_DEMOS": "OFF",
            "PLCOPEN_BUILD_PYTHON_BINDINGS": "OFF",
            "PLCOPEN_BUILD_DOCS": "OFF",
        })
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
