add_library(${PROJECT_NAME} SHARED
	wingspan-mem2reg.cpp
    plugin-registration.cpp
    wingspan-find-constant-operations.cpp 
    wingspan-constant-folder.cpp
    wingspan-find-identities.cpp
    wingspan-strength-reducer.cpp
)

llvm_map_components_to_libnames(llvmLibs Core Passes Support)
target_link_libraries(${PROJECT_NAME} PUBLIC ${llvmLibs})

# mio
#target_include_directories(${PROJECT_NAME} PUBLIC ${LLVM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED TRUE)

install(TARGETS ${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}.def)