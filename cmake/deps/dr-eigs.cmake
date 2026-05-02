if(TARGET dr::eigs)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    dr-eigs
    GIT_REPOSITORY https://github.com/davreev/dr-eigs.git
    GIT_TAG d6affedd0f9f9f24282d530bf94e826eb72ab969
)

FetchContent_MakeAvailable(dr-eigs)
