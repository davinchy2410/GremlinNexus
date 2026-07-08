# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\GremblingCore_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\GremblingCore_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\GremblingEx_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\GremblingEx_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\GremblingOutput_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\GremblingOutput_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\GremblingProcessing_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\GremblingProcessing_autogen.dir\\ParseCache.txt"
  "GremblingCore_autogen"
  "GremblingEx_autogen"
  "GremblingOutput_autogen"
  "GremblingProcessing_autogen"
  )
endif()
