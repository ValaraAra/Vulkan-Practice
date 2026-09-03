#include "utility.h"

#include <fstream>
#include <sstream>

// Returns empty string on file open failure.
std::string readTextFile(const std::string& filePath)
{
	std::ifstream file(filePath, std::ios::in | std::ios::binary);
	if (!file) { return std::string(); }

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}