#pragma once

#include <string>
#include <cctype>

#include <fstream>
#include <iostream>
#include <vector>
#include <map>


struct ReadBuffer
{
	ReadBuffer(const std::vector<char>& host);
	ReadBuffer(const ReadBuffer& from);


	ReadBuffer slice(unsigned int offset, unsigned int length);

	ReadBuffer advance(unsigned int count);

	unsigned int size()
	{
		return (unsigned int)(end - start);
	}

	const char* start;
	const char* end;
};

struct FourCC
{
	char data[4];
};

struct DashField
{
	DashField(ReadBuffer rb, bool topmost = true);

	void print(int depth);

	FourCC label;
	std::vector<char> data;
	std::vector<DashField> fields;
};

bool read_dash(const std::string & filename);
bool load_file(const std::string& filename, std::vector<char>& data);
