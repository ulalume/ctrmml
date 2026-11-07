//! \file wave.h
#ifndef WAVE_H
#define WAVE_H
#include "core.h"
#include <string>
#include <vector>
#include <stdint.h>

//! Wave file
class Wave_File
{
	friend class Wave_Bank;
	public:
		Wave_File(uint16_t channels=0, uint32_t rate=0, uint16_t bits=0);
		Wave_File(const std::string& filename);

		virtual ~Wave_File();

		// Methods to modify waveforms
		int read(const std::string& filename);
		int read(const uint8_t* filebuf, uint32_t filesize);
		void add_sample(const int16_t* sample, int count);

		const std::string& get_error() const;

		//int save(const std::string& filename);

	private:
		static int load_file(const std::string& filename, std::vector<uint8_t>& buffer);
		uint32_t parse_chunk(const uint8_t* fdata);

		uint16_t channels;
		uint16_t stype;
		uint16_t sbits;
		uint32_t srate;
		uint32_t slength;
		uint16_t step;

		bool use_smpl_chunk;
		int32_t transpose;
		uint32_t lstart;
		uint32_t lend;

		// data[channel][n]
		std::vector<std::vector<int16_t>> data;

		std::string error_message;
};

//! Base wave rom bank
class Wave_Bank
{
	public:
		//! Aggregate sample header class.
		struct Sample
		{
			uint32_t position;
			uint32_t start;
			uint32_t size;
			uint32_t loop_start;
			uint32_t loop_end;
			uint32_t rate;
			int32_t transpose;
			uint32_t flags;

			// aggregate type - no constructor
			void from_bytes(std::vector<uint8_t> input);
			std::vector<uint8_t> to_bytes() const;
		};

		//! A sample file converted to wave ROM data.
		struct Encoded_Sample
		{
			//! Header for the whole file, before the parameters of a sample definition.
			Sample header;
			std::vector<uint8_t> data;
		};
		//! Encoded samples keyed by file name.
		typedef std::map<std::string, Encoded_Sample> Encoded_Sample_Map;

		Wave_Bank(unsigned long max_size, unsigned long bank_size = 0);
		virtual ~Wave_Bank();

		// Helper methods
		void set_include_paths(const Tag& tag);
		void set_encoded_samples(const Encoded_Sample_Map* samples);
		const Encoded_Sample& get_encoded_sample(const std::string& filename, Encoded_Sample& buffer);

		// Methods to modify wave ROM memory
		unsigned int add_sample(const Tag& tag);
		unsigned int add_sample(Sample header, const std::vector<uint8_t>& sample);

		// Methods to get wave ROM memory
		const std::vector<Sample>& get_sample_headers();
		const std::vector<uint8_t>& get_rom_data();
		unsigned int get_max_size();
		unsigned int get_free_bytes();
		unsigned int get_total_gap();
		unsigned int get_largest_gap();
		const std::string& get_error();

	protected:
		struct Gap
		{
			unsigned long start;
			unsigned long end;
		};

		static const uint32_t NO_FIT = (uint32_t)-1;

		static int read_sample_file(const std::string& filename, const Tag& include_paths, Wave_File& wave);
		unsigned int find_gap(const Sample& header, uint32_t& gap_start) const;
		virtual std::vector<uint8_t> encode_sample(const std::string& encoding_type, const std::vector<int16_t>& input);
		virtual uint32_t fit_sample(const Sample& header, uint32_t start, uint32_t end) const;
		virtual int find_duplicate(const Sample& header, const std::vector<uint8_t>& sample) const;
		bool rom_data_equals(uint32_t position, const std::vector<uint8_t>& sample) const;
		void grow_rom_data();

		unsigned long max_size;
		unsigned long current_size;
		unsigned long bank_size;

		Tag include_paths;
		const Encoded_Sample_Map* encoded_samples;
		std::vector<uint8_t> rom_data;
		std::vector<Gap> gaps;
		std::vector<Sample> samples;
		std::string error_message;
};

#endif
