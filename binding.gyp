{
  "targets": [
    {
      "target_name": "macos_smc_native",
      "sources": [
        "lib/src/addon.c",
        "lib/src/smc.c",
        "lib/src/ioreport.c",
        "lib/src/sys.c"
      ],
      "cflags": ["-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter"],
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "12.0",
        "OTHER_CFLAGS": ["-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter"]
      },
      "libraries": [
        "-framework CoreFoundation",
        "-framework IOKit",
        "-ldl"
      ]
    }
  ]
}
