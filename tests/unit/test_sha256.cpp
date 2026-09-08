#include <doctest.h>

#include "utils/file_utils.h"
#include "utils/sha256.h"

using namespace promatte;

TEST_CASE("sha256 known vectors")
{
	CHECK(Sha256::hashString("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Sha256::hashString("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	std::string million(1000000, 'a');
	CHECK(Sha256::hashString(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256 of a file matches string hash")
{
	std::string path = fs::joinPath(fs::joinPath(PROMATTE_SOURCE_DIR, "build"), "sha_test.tmp");
	fs::createDirectories(fs::joinPath(PROMATTE_SOURCE_DIR, "build"));
	REQUIRE(fs::writeTextFile(path, "The quick brown fox jumps over the lazy dog"));
	CHECK(Sha256::hashFile(path) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
	fs::removeFile(path);
	CHECK(Sha256::hashFile(path).empty());
}
