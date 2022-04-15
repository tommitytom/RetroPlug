#include <catch/catch.hpp>

#include <iostream>

#include "core/FileManager.h"
#include "util/fs.h"

using namespace rp;
namespace fs = std::filesystem;

TEST_CASE("calculate unique filenames", "[FileManager]") {
	FileManager fileManager;
	fileManager.setRootPath("test");

	fs::remove_all("test");

	fs::path name = fileManager.getUniqueFilename("foo.txt");
	REQUIRE(name == "test\\0-foo.txt");

	name = fileManager.getUniqueFilename("0-foo.txt");
	REQUIRE(name == "test\\1-foo.txt");

	REQUIRE(fsutil::writeTextFile("test\\0-foo.txt", "hi"));

	name = fileManager.getUniqueFilename("foo.txt");
	REQUIRE(name == "test\\1-foo.txt");

	REQUIRE(fsutil::writeTextFile("test\\1-foo.txt", "hi"));

	name = fileManager.getUniqueFilename("foo.txt");
	REQUIRE(name == "test\\2-foo.txt");

	name = fileManager.getUniqueFilename("2-foo.txt");
	REQUIRE(name == "test\\3-foo.txt");

	name = fileManager.getUniqueFilename("01234-foo.txt");
	REQUIRE(name == "test\\1235-foo.txt");

	fs::remove_all("test");
}

TEST_CASE("calculate unique directory names", "[FileManager]") {
	FileManager fileManager;
	fileManager.setRootPath("test");

	fs::remove_all("test");

	fs::path name = fileManager.getUniqueDirectoryName("/");
	REQUIRE(name == "test\\0");

	REQUIRE(fs::create_directories("test\\0"));

	name = fileManager.getUniqueDirectoryName("/");
	REQUIRE(name == "test\\1");

	name = fileManager.getUniqueDirectoryName("projects/");
	REQUIRE(name == "test\\projects\\0");

	name = fileManager.getUniqueDirectoryName("/foo");
	REQUIRE(name == "test\\0-foo");

	REQUIRE(fs::create_directories("test\\0-foo"));

	name = fileManager.getUniqueDirectoryName("/foo");
	REQUIRE(name == "test\\1-foo");

	name = fileManager.getUniqueDirectoryName("/0-foo");
	REQUIRE(name == "test\\1-foo");

	fs::remove_all("test");
}
