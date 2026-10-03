Import("env")

# SCons keeps -fsanitize in the compile flags only; the link needs it too.
env.Append(LINKFLAGS=["-fsanitize=address,undefined"])
