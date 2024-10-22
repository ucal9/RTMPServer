#include <iostream>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

void rotating_example()
{
    // Create a file rotating logger with 5mb size max and 3 rotated files
    auto max_size = 1048576 * 5;
    auto max_files = 3;
    auto logger = spdlog::rotating_logger_mt("log_file", "logs/rotating.txt", max_size, max_files);

    for (int index = 0; index < 10; index++)
    {
        logger->info("index = {}", index);
        spdlog::info("index = {}", index);
    }
}

int main(int argc, char *argv[])
{ 
    rotating_example();
    spdlog::info("hello world!");

    return 0;
}