/*
	This code is a bit ugly and should be refactored when I have the time...
	2020-04-11: This code has now been slightly refactored. It is however still a little bit ugly.
*/
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wave.h"
#include "input.h"
#include "vgm.h"
#include "stringf.h"
#include "util.h"

static bool operator==(const Wave_Bank::Sample& s1, const Wave_Bank::Sample& s2)
{
	// Can't directly compare structs properly, so we convert to bytes instead
	// and then compare the arrays.
	return s1.to_bytes() == s2.to_bytes();
}

//=====================================================================

Wave_File::Wave_File(uint16_t channels, uint32_t rate, uint16_t bits)
	: channels(channels)
	, sbits(bits)
	, srate(rate)
	, use_smpl_chunk(0)
	, transpose(0)
	, lstart(0)
	, lend(0)
{
	stype = 1;
	step = channels*(bits/8);
}

Wave_File::~Wave_File()
{
	return;
}

//! Read a WAV file from disk.
/*!
 *  \return 0 on success, -1 on failure. See get_error() for the reason.
 */
int Wave_File::read(const std::string& filename)
{
	std::vector<uint8_t> file;
	if(load_file(filename, file))
	{
		error_message = "cannot open file";
		return -1;
	}
	return read(file.data(), file.size());
}

//! Read a WAV file from memory.
/*!
 *  \return 0 on success, -1 on failure. See get_error() for the reason.
 */
int Wave_File::read(const uint8_t* filebuf, uint32_t filesize)
{
	uint32_t pos=0, wavesize;
	channels = 0;
	error_message.clear();

	if(filesize < 13)
	{
		error_message = "malformed WAV file";
		return -1;
	}
	if(memcmp(&filebuf[0],"RIFF",4))
	{
		error_message = "RIFF header not found";
		return -1;
	}
	wavesize = (*(uint32_t*)(filebuf+4)) + 8;
	pos += 8;
	if(memcmp(&filebuf[pos],"WAVE",4))
	{
		error_message = "not a WAVE format file";
		return -1;
	}
	pos += 4;
	while(pos < wavesize)
	{
		uint32_t chunksize, ret;

		chunksize = *(uint32_t*)(filebuf+pos+4) + 8;
		if(pos+chunksize > filesize)
		{
			error_message = "illegal chunk size";
			return -1;
		}
		ret = parse_chunk(filebuf+pos);
		if(!ret)
		{
			if(error_message.empty())
				error_message = stringf("failed to parse chunk %c%c%c%c",filebuf[pos],filebuf[pos+1],filebuf[pos+2],filebuf[pos+3]);
			return -1;
		}
		pos += chunksize;

		if(pos & 1)
			pos++;
	}
	return 0;
}

//! Get the reason the last read() failed.
const std::string& Wave_File::get_error() const
{
	return error_message;
}

int Wave_File::load_file(const std::string& filename, std::vector<uint8_t>& buffer)
{
	if(std::ifstream is{filename, std::ios::binary|std::ios::ate})
	{
		auto size = is.tellg();
		buffer.resize(size);
		is.seekg(0);
		if(is.read((char*)buffer.data(), size))
			return 0;
	}
	return -1;
}

uint32_t Wave_File::parse_chunk(const uint8_t *fdata)
{
	uint32_t chunkid = *(uint32_t*)(fdata);
	uint32_t chunksize = *(uint32_t*)(fdata+4);
	//printf("Parse the %c%c%c%c chunk with %d size\n", fdata[0],fdata[1],fdata[2],fdata[3], chunksize);
	uint32_t pos = 0;
	switch(chunkid)
	{
		default:
			break;
		case 0x20746d66: // 'fmt '
			if(chunksize < 0x10)
				return 0;
			stype = *(uint16_t*)(fdata+0x08);
			channels = *(uint16_t*)(fdata+0x0a);
			sbits = *(uint16_t*)(fdata+0x16);
			step = (sbits * channels) / 8;
			srate = *(uint32_t*)(fdata+0x0c);
			slength = 0;
			if(stype != 1 || channels > 2 || step == 0)
			{
				error_message = "unsupported format";
				return 0;
			}
			if(data.size() != channels)
				data.resize(channels);
			break;
		case 0x61746164: // 'data'
			if(step == 0)
				return 0;
			for(const uint8_t* d = fdata+8; d < (fdata+8+chunksize);)
			{
				for(int ch=0; ch<channels; ch++)
				{
					if(sbits == 8)
					{
						data[ch].push_back((*d ^ 0x80) << 8);
						d++;
					}
					else if(sbits == 16)
					{
						data[ch].push_back(*(int16_t*)d);
						d += 2;
					}
				}
				pos++;
			}
			slength = data[0].size();
			lstart = 0;
			lend = 0;
			break;
		case 0x6c706d73: // 'smpl'
			use_smpl_chunk = 1;
			if(chunksize >= 0x10)
			{
				transpose = *(uint32_t*)(fdata+0x14);
				if(!transpose)
					transpose -= 60;
			}
			if(chunksize >= 0x2c && *(uint32_t*)(fdata+0x24))
			{
				lstart = *(uint32_t*)(fdata+0x2c+8);
				lend = *(uint32_t*)(fdata+0x2c+12) + 1;
				slength = lend;
			}
			break;
	}
	return chunksize;
}


//=====================================================================

//! Creates a Wave_Bank
Wave_Bank::Wave_Bank(unsigned long max_size, unsigned long bank_size)
	: max_size(max_size)
	, current_size(0)
	, bank_size(bank_size)
	, include_paths{""}
	, encoded_samples(nullptr)
	, rom_data()
	, gaps()
{
	rom_data.resize(max_size, 0);
	if(!bank_size)
		this->bank_size = max_size;
}

//! Wave_Bank destructor
Wave_Bank::~Wave_Bank()
{
	return;
}

//! Set a list of include paths to check when reading samples from a Tag.
void Wave_Bank::set_include_paths(const Tag& tag)
{
	include_paths = tag;
}

//! Set the encoded samples to use instead of reading their files.
/*!
 *  The map must remain valid while samples are added from Tags.
 */
void Wave_Bank::set_encoded_samples(const Encoded_Sample_Map* samples)
{
	encoded_samples = samples;
}

//! Read the file of a sample definition.
/*!
 *  The file is read from the first include path where it can be read as a
 *  WAV file.
 *
 *  \param filename  File name from the sample definition.
 *  \param include_paths Paths prepended to \p filename, in search order.
 *  \param wave      Receives the parsed file. If no file could be parsed,
 *                   get_error() returns the first parse error, or an empty
 *                   string if no file was found.
 *  \return 0 on success, -1 on failure.
 */
int Wave_Bank::read_sample_file(const std::string& filename, const Tag& include_paths, Wave_File& wave)
{
	std::string error;
	for(auto&& path : include_paths)
	{
		std::vector<uint8_t> file;
		if(Wave_File::load_file(path + filename, file))
			continue;
		wave = Wave_File();
		if(wave.read(file.data(), file.size()) == 0)
			return 0;
		if(error.empty())
			error = wave.get_error();
	}
	wave = Wave_File();
	wave.error_message = error;
	return -1;
}

//! Get the sample of a file, encoded for the wave ROM.
/*!
 *  A sample set with set_encoded_samples() is used before reading the file
 *  from the include paths.
 *
 *  \param filename File name from the sample definition.
 *  \param buffer   Receives the sample if it is read from the file.
 *  \return The encoded sample, or \p buffer.
 *  \exception InputError if the file cannot be read.
 */
const Wave_Bank::Encoded_Sample& Wave_Bank::get_encoded_sample(const std::string& filename, Encoded_Sample& buffer)
{
	if(encoded_samples)
	{
		auto it = encoded_samples->find(filename);
		if(it != encoded_samples->end())
			return it->second;
	}
	Wave_File wf;
	if(read_sample_file(filename, include_paths, wf))
	{
		if(wf.get_error().empty())
			error_message = filename + " not found";
		else
			error_message = filename + ": " + wf.get_error();
		throw InputError(nullptr, error_message.c_str());
	}
	buffer.data = encode_sample("", wf.data[0]);
	buffer.header = {
		0,
		0,
		wf.slength,
		wf.lstart,
		wf.lend,
		wf.srate,
		wf.transpose,
		0};
	return buffer;
}

//! Convert and add sample to the waverom.
unsigned int Wave_Bank::add_sample(const Tag& tag)
{
	if(!tag.size())
	{
		error_message = "Incomplete sample definition";
		throw InputError(nullptr, error_message.c_str());
	}
	Encoded_Sample buffer;
	const Encoded_Sample& sample = get_encoded_sample(tag[0], buffer);
	Wave_Bank::Sample header = sample.header;

	// Allow overriding the sample rate and setting start offset
	int i = 1;
	uint32_t param;
	while(tag.size() > i)
	{
		if(std::sscanf(tag[i].c_str(), "rate = %u", &param) == 1)
		{
			header.rate = param;
		}
		else if(std::sscanf(tag[i].c_str(), "offset = %u", &param) == 1)
		{
			if(param > header.size)
				throw InputError(nullptr, "Sample offset cannot be greater than total length");
			header.start += param;
			header.size -= param;
		}
		i++;
	}

	return add_sample(header, sample.data);
};

//! Add sample to the waverom in raw format.
unsigned int Wave_Bank::add_sample(Wave_Bank::Sample header, const std::vector<uint8_t>& sample)
{
	// Find duplicates of sample data and selected header parameters if needed
	int duplicate = find_duplicate(header, sample);

	if(duplicate != -1)
	{
		header.position = samples[duplicate].position;
		auto result = std::find(samples.begin(), samples.end(), header);
		if(result != samples.end())
		{
			// Header is similar to an existing one, we reuse it
			return result - samples.begin();
		}
		else
		{
			// Create a new header, while using the same sample data
			samples.push_back(header);
			return samples.size() - 1;
		}
	}
	else if(header.start && header.size)
	{
		// Store only the data from the start offset. Loop points keep
		// pointing at the same data, but not before the new start.
		auto begin = sample.begin() + header.start;
		std::vector<uint8_t> data(begin, begin + header.size);
		header.loop_start -= std::min(header.loop_start, header.start);
		header.loop_end -= std::min(header.loop_end, header.start);
		header.start = 0;
		return add_sample(header, data);
	}
	else
	{
		// Create a new entry.
		uint32_t start_pos = -1; // Aligned start position
		uint32_t start = current_size; // Proposed start position
		// Check if the sample fits in a gap.
		unsigned int gap_id = find_gap(header, start_pos);
		if(gap_id != NO_FIT)
		{
			start = gaps[gap_id].start;
			gaps[gap_id].start = start_pos + header.size;
		}
		else
		{
			// Append sample to the end
			start_pos = fit_sample(header, start, max_size);
		}
		// Check if sample fits in ROM
		if(start_pos == NO_FIT)
		{
			error_message = stringf("Sample does not fit in remaining ROM space (%d bytes remaining, sample size is %d)", max_size - start_pos, sample.size());
			throw InputError(nullptr, error_message.c_str());
		}
		// Add a new gap if needed
		if(start_pos > start)
		{
			gaps.push_back({start, start_pos});
		}
		// Move the end position if needed
		if(start_pos >= current_size)
		{
			current_size = start_pos + header.size;
		}

		std::copy_n(sample.begin(), header.size, rom_data.begin() + start_pos);
		header.position = start_pos;
		samples.push_back(header);
		return samples.size() - 1;
	}
}

//! Get sample headers
const std::vector<Wave_Bank::Sample>& Wave_Bank::get_sample_headers()
{
	return samples;
}

//! Get the sample ROM data
const std::vector<uint8_t>& Wave_Bank::get_rom_data()
{
	return rom_data;
}

//! Get the number of unused allocated bytes in the Wave_Bank.
unsigned int Wave_Bank::get_free_bytes()
{
	return max_size - current_size;
}

//! Get the total size of alignment gaps.
unsigned int Wave_Bank::get_total_gap()
{
	unsigned int gap_size = 0;
	for(auto&& gap : gaps)
	{
		gap_size += gap.end - gap.start;
	}
	return gap_size;
}

//! Get the size of the largest gap.
/*!
 *  If there are no gaps, return 0.
 */
unsigned int Wave_Bank::get_largest_gap()
{
	unsigned int largest_gap = 0;
	for(auto&& gap : gaps)
	{
		unsigned int gap_size = gap.end - gap.start;
		if(gap_size > largest_gap)
			largest_gap = gap_size;
	}
	return largest_gap;
}

//! Get error message
const std::string& Wave_Bank::get_error()
{
	return error_message;
}

//! Check if the sample fits in an existing alignment gap
/*!
 *  If the sample cannot fit in any gap, return NO_FIT. Otherwise, return
 *  the index of the smallest gap that fits the sample.
 *
 *  \p gap_start will be set with the aligned start position of the gap.
 */
unsigned int Wave_Bank::find_gap(const Wave_Bank::Sample& header, uint32_t& gap_start) const
{
	unsigned int best_gap = NO_FIT;
	if(gaps.size())
	{
		// Look for the smallest gap that fits our sample
		int best_gap_size = max_size;
		uint32_t start_pos;
		for(unsigned int i = 0; i < gaps.size(); i++)
		{
			int gap_size = gaps[i].end - gaps[i].start;
			start_pos = fit_sample(header, gaps[i].start, gaps[i].end);
			if(start_pos != NO_FIT && gap_size < best_gap_size)
			{
				best_gap = i;
				best_gap_size = gap_size;
				gap_start = start_pos;
			}
		}
	}
	return best_gap;
}

//! Encode the sample, convert it from 16-bit data to 8-bit.
std::vector<uint8_t> Wave_Bank::encode_sample(const std::string& encoding_type, const std::vector<int16_t>& input)
{
	// default encoder, simply convert 16-bit to 8-bit unsigned
	std::vector<uint8_t> output;
	for(auto&& i : input)
	{
		output.push_back((i >> 8) ^ 0x80);
	}
	return output;
}

//! Returns the next possible aligned start address for the rom.
/*!
 *  Given a sample header, a proposed start address and end address,
 *  return the appropriate start address of the sample. If the sample
 *  cannot fit within the boundaries. return NO_FIT.
 *
 *  TODO: This code is optimized for MDSDRV with mid playback bank
 *  switches only supported for 17.5khz sample rate for now.
 *  When adding more sound drivers, this will have to be a generic
 *  function that will reject samples crossing banks, with bank
 *  crossing behavior determined on a per-driver basis.
 */
uint32_t Wave_Bank::fit_sample(const Wave_Bank::Sample& header, uint32_t start, uint32_t end) const
{
	uint32_t sample_end = start + header.size;
	uint32_t start_bank = start / bank_size;
	uint32_t end_bank = sample_end / bank_size;
	// Adjust start address for bank crossing.
	// Smarter method if sample is only to be played at the same sample rate.
	// I will use this by default if the sample is too big to fit in a Z80 bank anyway.
	if (start_bank != end_bank && header.size > bank_size)
		start = (start + 0x1f) & 0xffffffe0;
	// Alternate behavior for MDSDRV. More ROM space but will handle sample rate switches.
	else if (start_bank != end_bank && (start % bank_size) != 0)
		start = (start_bank + 1) * bank_size;
	// Crossed end boundary?
	if ((start + header.size) > end)
		return NO_FIT;
	return start;
}

//! Look for duplicates in the sample ROM.
/*!
 *  If a duplicate is found, return the index to the duplicate wave entry.
 *  Otherwise, return -1.
 */
int Wave_Bank::find_duplicate(const Wave_Bank::Sample& header, const std::vector<uint8_t>& sample) const
{
	int id = 0;
	for(auto&& i : samples)
	{
		// The reason for the loop start check is that some sound chips (like C352)
		// require that the looping part of the sample fit in the same bank.
		if(   i.position + sample.size() <= rom_data.size()
		   && i.loop_start <= header.loop_start
		   && !memcmp(&sample[0], &rom_data[i.position], sample.size()))
			return id;
		id++;
	}
	return -1;
}

//=====================================================================
//! Fill a sample header with values from a byte vector.
/*!
 *  The format matches the data created by to_bytes().
 *
 *  \exception std::out_of_range Input too small
 */
void Wave_Bank::Sample::from_bytes(std::vector<uint8_t> input)
{
	position = read_le32(input, 0);
	start = read_le32(input, 4);
	size = read_le32(input, 8);
	loop_start = read_le32(input, 12);
	loop_end = read_le32(input, 16);
	rate = read_le32(input, 20);
	transpose = read_le32(input, 24);
	flags = read_le32(input, 28);
}

//! Return a sample header as a byte vector.
/*!
 *  Output can be made as a header again using to_bytes().
 */
std::vector<uint8_t> Wave_Bank::Sample::to_bytes() const
{
	auto output = std::vector<uint8_t>();
	write_le32(output, 0, position);
	write_le32(output, 4, start);
	write_le32(output, 8, size);
	write_le32(output, 12, loop_start);
	write_le32(output, 16, loop_end);
	write_le32(output, 20, rate);
	write_le32(output, 24, transpose);
	write_le32(output, 28, flags); // Reserved.
	return output;
}
