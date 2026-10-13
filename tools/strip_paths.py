"""Keep the build machine's paths out of the image: the release is public.

__FILE__ in the core's asserts and logs carries the packages directory
(C:/Users/<name>/.platformio/...), so each prefix is mapped to a short name,
in both slash styles, since Windows builds see either.
"""
Import("env")  # noqa: F821

maps = {
    env.subst("$PROJECT_PACKAGES_DIR"): "pio",  # noqa: F821
    env.subst("$PROJECT_DIR"): ".",  # noqa: F821
}
flags = []
for path, short in maps.items():
    for spelling in dict.fromkeys([path, path.replace("\\", "/")]):   # ordered, so the command line is stable
        flags.append("-ffile-prefix-map=%s=%s" % (spelling, short))
env.Append(CCFLAGS=flags)  # noqa: F821
