from conans import ConanFile, CMake, tools


class OpenVdbConan(ConanFile):
    name = "openvdb"
    version = "10.0.1"
    license = "<Put the package license here>"
    author = "Josiah Ebhomenye joebhomenye@gmail.com"
    url = "<Package recipe repository url here, for issues about the package>"
    description = "vulkan utility library"
    topics = ("vulkan", "graphics", "3d")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = "cmake"
    requires = []

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def source(self):
        self.run("git clone git@github.com:AcademySoftwareFoundation/openvdb.git openvdb")
        self.run("git -C \"%s/openvdb\" checkout v%s" % (self.source_folder, self.version))

    def build(self):
        self.run('conan install \"%s/openvdb\" -s build_type=%s' % (self.source_folder, self.settings.build_type.value))
        cmake = CMake(self)
        cmake.configure(source_folder="openvdb")
        # cmake.build()

        # Explicit way:
        self.run('cmake \"%s/openvdb\" %s'
                 % (self.source_folder, cmake.command_line))
        self.run("cmake \"%s/openvdb\" -D BUILD_EXAMPLES:BOOL=OFF" % self.source_folder)
        self.run("cmake --build . %s" % cmake.build_config)

    def package(self):
        self.copy("VulkanBase/*.h*", dst="include/openvdb", src="openvdb", keep_path=False)
        self.copy("3rdParty/*.h*", dst="include/openvdb", src="openvdb", keep_path=False)
        self.copy("ImGuiPlugin/*.h*", dst="include/openvdb", src="openvdb", keep_path=False)
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = ["VulkanBase", "ImGuiPlugin"]

