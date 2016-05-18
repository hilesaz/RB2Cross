#include "dash.h"


inline ReadBuffer::ReadBuffer(const std::vector<char>& host)
{
	start = &host[0];
	end = start + host.size();
}

inline ReadBuffer::ReadBuffer(const ReadBuffer & from)
{
	start = from.start;
	end = from.end;
}

inline ReadBuffer ReadBuffer::slice(unsigned int offset, unsigned int length)
{
	if (length + offset > size())
	{
		throw std::exception("tried to slice more off buffer than exists");
	}
	ReadBuffer ret = *this;
	ret.start += offset;
	ret.end = ret.start + length;
	return ret;
}

inline ReadBuffer ReadBuffer::advance(unsigned int count)
{
	if (count > size())
	{
		throw std::exception("Advanced past the end of buffer");
	}
	ReadBuffer ret = *this;
	ret.start += count;
	return ret;
}



bool load_file(const std::string& filename, std::vector<char>& data)
{
	std::ifstream ifs(filename, std::ios::binary);
	if (!ifs)
		return false;
	auto start = ifs.tellg();
	ifs.seekg(0, ifs.end);
	auto end = ifs.tellg();
	ifs.seekg(0, ifs.beg);

	data.resize(end - start);

	ifs.read(&data[0], end - start);
	return true;
}



unsigned int read_uint(const char* start)
{
	return ((unsigned char)start[0]) << 24 | ((unsigned char)start[1]) << 16 | ((unsigned char)start[2]) << 8 | ((unsigned char)start[3]);
}

//checks the next 8 bytes
bool is_likely_dash_subfield(ReadBuffer rb)
{
	//length is too big
	if (read_uint(rb.start) > rb.size())
		return false;
	//Fourcc has non-printable characters?
	for (const char* d = rb.start + 4; d < rb.start + 8; ++d)
	{
		if(*d < 0 || !std::isprint(*d))
			return false;
	}
	return true;
}


DashField::DashField(ReadBuffer rb, bool topmost)
{
	ReadBuffer rbdata = rb;
	if (!topmost)
	{
		if (rb.size() < 4)
			throw std::exception("field not large enough to hold length");
		auto length = read_uint(rb.start);

		if (rb.size() < length)
			throw std::exception("field not large enough to hold payload");
		std::copy(rb.start + 4, rb.start + 8, label.data);
		rbdata = rb.advance(8);
	}
	else
	{
		char buf[4] = { 'r','o','o','t' };
		std::copy(buf, buf + 4, label.data);
	}

	while (rbdata.size() > 8 && is_likely_dash_subfield(rbdata))
	{
		auto length = read_uint(rbdata.start);
		ReadBuffer subdata = rbdata.slice(0, length);
		fields.emplace_back(subdata, false);
		rbdata = rbdata.advance(length);
	}
	data.assign(rbdata.start, rbdata.end);
}

void DashField::print(int depth)
{
	std::string indent(depth, '\t');
	(std::cout << indent).write(label.data, 4) << ", size: " << data.size() << std::endl;
	for (auto subfield : fields)
	{
		subfield.print(depth + 1);
	}
}

bool read_dash(const std::string & filename)
{
	std::vector<char> data;
	if (!load_file(filename, data))
		return false;

	ReadBuffer rb(data);

	//Ignore exceptions from this to get call stack
	DashField root(rb);

	root.print(0);
}
