Import("env")

env.Append(
  LINKFLAGS=[
      "-mfloat-abi=hard",
      "-mfpu=fpv5-d16"
  ]
)
