if(TARGET dr::eigs)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    dr-eigs
    URL https://github.com/davreev/dr-eigs/archive/refs/tags/0.1.0.zip
)

FetchContent_MakeAvailable(dr-eigs)
