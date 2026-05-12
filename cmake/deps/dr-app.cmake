if(TARGET dr::app)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    dr-app
    GIT_REPOSITORY https://github.com/davreev/dr-app.git
    GIT_TAG 5a08614926ea8e8b17f880eb985b1bfd12fffb78
)

FetchContent_MakeAvailable(dr-app)
