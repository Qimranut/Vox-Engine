/*************************************************************************************************************
\file       voxmap_io.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Reads and writes .voxmap files: header, material names, section directory and per-section
            payloads, with an atomic .tmp-and-rename save and a fully bounds-checked parse.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxmap_io.hpp"

// Voxel
#include "voxmap_format.hpp"
#include "voxmap_codec.hpp"

// Windows, for the atomic rename
#include <windows.h>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>


namespace voxel::voxmap
{
	namespace
	{
		class Writer
		{
		public:

			std::vector<unsigned char> bytes_;

			/*!
				@brief
					Appends one byte.
				@param v
					Byte to write
				*/
			void u8(std::uint8_t v)		{ bytes_.push_back(v); }

			/*!
				@brief
					Appends a 16-bit value, little-endian.
				@param v
					Value to write
				*/
			void u16(std::uint16_t v)	{ u8(v & 0xFF); u8(static_cast<std::uint8_t>(v >> 8)); }

			/*!
				@brief
					Appends a 32-bit value, little-endian.
				@param v
					Value to write
				*/
			void u32(std::uint32_t v)	{ for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }

			/*!
				@brief
					Appends a 64-bit value, little-endian.
				@param v
					Value to write
				*/
			void u64(std::uint64_t v)	{ for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i))); }

			/*!
				@brief
					Appends a float as its 32-bit pattern, little-endian.
				@param v
					Value to write
				*/
			void f32(float v)			{ std::uint32_t b; std::memcpy(&b, &v, 4); u32(b); }

			/*!
				@brief
					Appends zero bytes until the buffer size is a multiple of 4.
				*/
			void pad4()					{ while (bytes_.size() % 4) u8(0); }

			/*!
				@brief
					Appends a length-prefixed string padded to 4 bytes.
				@param s
					String to write
				*/
			void str(const std::string& s)
			{
				u32(static_cast<std::uint32_t>(s.size()));
				bytes_.insert(bytes_.end(), s.begin(), s.end());
				pad4();
			}

			/*!
				@brief
					Overwrites 4 already-written bytes with a 32-bit value, little-endian.
				@param at
					Byte offset to overwrite
				@param v
					Value to write
				*/
			void patchU32(std::size_t at, std::uint32_t v)
			{
				for (int i = 0; i < 4; ++i)
					bytes_[at + i] = static_cast<unsigned char>(v >> (8 * i));
			}

			/*!
				@brief
					Overwrites 8 already-written bytes with a 64-bit value, little-endian.
				@param at
					Byte offset to overwrite
				@param v
					Value to write
				*/
			void patchU64(std::size_t at, std::uint64_t v)
			{
				for (int i = 0; i < 8; ++i)
					bytes_[at + i] = static_cast<unsigned char>(v >> (8 * i));
			}
		};

		class Reader
		{
		public:

			/*!
				@brief
					Starts reading at the beginning of a byte span.
				@param b
					Bytes to read from; must outlive the reader
				*/
			explicit Reader(std::span<const unsigned char> b) : b_(b) {}

			std::size_t pos_{ 0 };

			/*!
				@brief
					Throws unless n more bytes are available from the current position.
				@param n
					Number of bytes about to be read
				*/
			void need(std::size_t n) const
			{
				if (pos_ + n > b_.size())
					throw std::runtime_error("voxmap: truncated file");
			}

			/*!
				@brief
					Reads one byte.
				@return
					The byte
				*/
			std::uint8_t u8()	{ need(1); return b_[pos_++]; }

			/*!
				@brief
					Reads a little-endian 16-bit value.
				@return
					The value
				*/
			std::uint16_t u16()	{ need(2); std::uint16_t v = static_cast<std::uint16_t>(b_[pos_] | (b_[pos_ + 1] << 8)); pos_ += 2; return v; }

			/*!
				@brief
					Reads a little-endian 32-bit value.
				@return
					The value
				*/
			std::uint32_t u32()	{ need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b_[pos_ + i]) << (8 * i); pos_ += 4; return v; }

			/*!
				@brief
					Reads a little-endian 64-bit value.
				@return
					The value
				*/
			std::uint64_t u64()	{ need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b_[pos_ + i]) << (8 * i); pos_ += 8; return v; }

			/*!
				@brief
					Reads a float stored as its little-endian 32-bit pattern.
				@return
					The value
				*/
			float f32()			{ const std::uint32_t b = u32(); float f; std::memcpy(&f, &b, 4); return f; }

			/*!
				@brief
					Skips forward to the next multiple of 4 bytes.
				*/
			void pad4()			{ while (pos_ % 4) ++pos_; }

			/*!
				@brief
					Reads a length-prefixed string (at most 4096 bytes) and its padding.
				@return
					The string
				*/
			std::string str()
			{
				const std::uint32_t n = u32();
				if (n > 4096)
					throw std::runtime_error("voxmap: string too long");
				need(n);
				std::string s(reinterpret_cast<const char*>(b_.data() + pos_), n);
				pos_ += n;
				pad4();
				return s;
			}

			/*!
				@brief
					Returns a bounds-checked view of a payload inside the file; throws if it runs outside.
				@param offset
					Byte offset from the file start
				@param size
					Number of bytes
				@return
					The payload bytes
				*/
			std::span<const unsigned char> slice(std::uint64_t offset, std::uint32_t size) const
			{
				if (offset > b_.size() || size > b_.size() - offset)
					throw std::runtime_error("voxmap: payload outside the file");
				return b_.subspan(static_cast<std::size_t>(offset), size);
			}

		private:

			std::span<const unsigned char> b_;
		};

		/*!
			@brief
				Tests whether every voxel of a dense section holds the same material.
			@param data
				kSectionVolume material values
			@param value
				Receives the first voxel's material
			@return
				true when all voxels equal the first
			*/
		bool allEqual(const std::uint8_t* data, std::uint8_t& value)
		{
			value = data[0];
			for (int i = 1; i < kSectionVolume; ++i)
				if (data[i] != value)
					return false;
			return true;
		}
	}

	/*!
		@brief
			Serialises level 0 of a world into .voxmap bytes.

			Dense sections whose voxels are all equal are written as uniform
			without touching the world. Sections are written in sectionIndex()
			order whatever the world's paging.
		@param world
			World to serialise
		@param materials
			Material table whose names are stored; file id == runtime id
		@param info
			Seed, material hash (0 = use the table's) and recipe path to record
		@return
			The file bytes
		*/
	std::vector<unsigned char> write(const World& world, const MaterialTable& materials, const Info& info)
	{
		Writer w;
		const WorldDims& d = world.dims();

		w.u32(kMagic);
		w.u32(kVersion);
		const std::size_t headerBytesAt = w.bytes_.size();
		w.u32(0);
		w.f32(d.voxelSize_);
		w.f32(d.origin_.x); w.f32(d.origin_.y); w.f32(d.origin_.z);
		w.u32(static_cast<std::uint32_t>(d.voxels_.x)); w.u32(static_cast<std::uint32_t>(d.voxels_.y)); w.u32(static_cast<std::uint32_t>(d.voxels_.z));
		w.u32(static_cast<std::uint32_t>(d.sections_.x)); w.u32(static_cast<std::uint32_t>(d.sections_.y)); w.u32(static_cast<std::uint32_t>(d.sections_.z));
		w.u32(kCompressionNone);
		w.u32(0);	// flags
		w.u64(info.seed_);
		w.u64(info.materialHash_ ? info.materialHash_ : materials.hash());
		w.str(info.recipePath_);
		w.patchU32(headerBytesAt, static_cast<std::uint32_t>(w.bytes_.size()));

		// Materials: file id == runtime id, by construction.
		w.u32(static_cast<std::uint32_t>(materials.size()));
		for (const Material& m : materials.materials())
			w.str(m.name_);

		// Directory placeholder.
		// In sectionIndex() order, whatever the world's paging.
		const std::size_t sectionCount = world.sectionCount(0);
		w.u32(static_cast<std::uint32_t>(sectionCount));
		const std::size_t dirAt = w.bytes_.size();
		w.bytes_.resize(dirAt + sectionCount * sizeof(DirEntry), 0);

		std::vector<unsigned char> payload;
		for (std::size_t i = 0; i < sectionCount; ++i)
		{
			const Section& s = world.sectionAt(0, i);
			DirEntry e;

			std::uint8_t uniform = 0;
			if (s.isUniform())
			{
				e.kind_ = kKindUniform;
				e.uniformMaterial_ = s.uniformMaterial();
			}
			else if (allEqual(s.data(), uniform))
			{
				e.kind_ = kKindUniform;
				e.uniformMaterial_ = uniform;
			}
			else
			{
				payload.clear();
				e.codec_ = encodeSection(s.data(), payload);
				e.kind_ = kKindPalette;
				w.pad4();
				e.offset_ = w.bytes_.size();
				e.size_ = static_cast<std::uint32_t>(payload.size());
				w.bytes_.insert(w.bytes_.end(), payload.begin(), payload.end());
			}

			const std::size_t at = dirAt + i * sizeof(DirEntry);
			w.patchU64(at, e.offset_);
			w.patchU32(at + 8, e.size_);
			w.bytes_[at + 12] = static_cast<unsigned char>(e.uniformMaterial_ & 0xFF);
			w.bytes_[at + 13] = static_cast<unsigned char>(e.uniformMaterial_ >> 8);
			w.bytes_[at + 14] = e.kind_;
			w.bytes_[at + 15] = e.codec_;
		}

		w.pad4();
		w.u32(kTrailer);
		return std::move(w.bytes_);
	}

	/*!
		@brief
			Writes a world to disk through a .tmp file and an atomic rename.

			Creates the parent folder. Throws std::runtime_error on failure and
			removes the .tmp file.
		@param path
			Destination .voxmap path
		@param world
			World to serialise
		@param materials
			Material table whose names are stored
		@param info
			Provenance to record
		*/
	void writeFile(const std::filesystem::path& path, const World& world, const MaterialTable& materials, const Info& info)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const std::vector<unsigned char> bytes = write(world, materials, info);

		std::filesystem::create_directories(path.parent_path());

		const std::filesystem::path tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
				throw std::runtime_error("voxmap: cannot open " + tmp.string() + " for writing");
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!out)
			{
				out.close();
				std::filesystem::remove(tmp);
				throw std::runtime_error("voxmap: write failed for " + tmp.string());
			}
		}

		if (!MoveFileExW(tmp.wstring().c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::error_code ec;
			std::filesystem::remove(tmp, ec);
			throw std::runtime_error("voxmap: cannot replace " + path.string());
		}
	}

	/*!
		@brief
			Parses a .voxmap into a new world.

			File material names are remapped onto the runtime table; unknown
			names fall back to runtime id 1 with a warning, and a changed table
			hash adds a warning too. Throws std::runtime_error on a corrupt file.
		@param bytes
			The whole file
		@param materials
			Runtime material table
		@return
			The world, its provenance and any warnings
		*/
	ReadResult read(std::span<const unsigned char> bytes, const MaterialTable& materials)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		Reader r(bytes);
		ReadResult result;

		if (r.u32() != kMagic)
			throw std::runtime_error("voxmap: bad magic");
		const std::uint32_t version = r.u32();
		if (version != kVersion)
			throw std::runtime_error("voxmap: unsupported version " + std::to_string(version));
		const std::uint32_t headerBytes = r.u32();

		WorldDims dims;
		dims.voxelSize_ = r.f32();
		dims.origin_.x = r.f32(); dims.origin_.y = r.f32(); dims.origin_.z = r.f32();
		dims.voxels_.x = static_cast<int>(r.u32()); dims.voxels_.y = static_cast<int>(r.u32()); dims.voxels_.z = static_cast<int>(r.u32());
		dims.sections_.x = static_cast<int>(r.u32()); dims.sections_.y = static_cast<int>(r.u32()); dims.sections_.z = static_cast<int>(r.u32());

		const std::uint32_t compression = r.u32();
		if (compression != kCompressionNone)
			throw std::runtime_error("voxmap: unsupported compression");
		r.u32();	// flags
		result.info_.seed_ = r.u64();
		result.info_.materialHash_ = r.u64();
		result.info_.recipePath_ = r.str();

		if (headerBytes != r.pos_)
			throw std::runtime_error("voxmap: header size mismatch");

		if (dims.voxelSize_ <= 0.f || dims.voxels_.x <= 0 || dims.voxels_.y <= 0 || dims.voxels_.z <= 0)
			throw std::runtime_error("voxmap: invalid dimensions");

		const WorldDims expected = WorldDims::fromVoxels(dims.voxels_, dims.voxelSize_, dims.origin_);
		if (expected.sections_ != dims.sections_)
			throw std::runtime_error("voxmap: section grid does not match the voxel extent");

		if (dims.sections_.x > 4096 || dims.sections_.y > 4096 || dims.sections_.z > 4096)
			throw std::runtime_error("voxmap: unreasonable section grid");

		// Materials by name.
		const std::uint32_t materialCount = r.u32();
		if (materialCount == 0 || materialCount > 65536)
			throw std::runtime_error("voxmap: bad material count");

		std::vector<MaterialId> fileToRuntime(materialCount);
		const MaterialId fallback = materials.size() > 1 ? MaterialId{ 1 } : kAir;
		for (std::uint32_t i = 0; i < materialCount; ++i)
		{
			const std::string name = r.str();
			MaterialId id = materials.id(name);
			if (id == kUnknownMaterial)
			{
				result.warnings_.push_back("unknown material \"" + name + "\" -> \"" + materials.get(fallback).name_ + "\"");
				id = fallback;
			}
			fileToRuntime[i] = id;
		}

		if (result.info_.materialHash_ != materials.hash())
			result.warnings_.push_back("material table changed since this map was cooked");

		const auto remap = [&fileToRuntime](std::uint16_t fileId) -> MaterialId
		{
			return fileId < fileToRuntime.size() ? fileToRuntime[fileId] : kAir;
		};

		auto world = std::make_unique<World>(dims);

		const std::uint32_t dirCount = r.u32();
		if (dirCount != world->sectionCount(0))
			throw std::runtime_error("voxmap: directory count does not match the section grid");

		std::vector<DirEntry> dir(dirCount);
		for (DirEntry& e : dir)
		{
			e.offset_ = r.u64();
			e.size_ = r.u32();
			e.uniformMaterial_ = r.u16();
			e.kind_ = r.u8();
			e.codec_ = r.u8();
		}

		for (std::size_t i = 0; i < dir.size(); ++i)
		{
			const DirEntry& e = dir[i];
			if (e.kind_ == kKindUniform)
			{
				world->editSectionAt(0, i) = Section::uniform(remap(e.uniformMaterial_));
				continue;
			}
			if (e.kind_ != kKindPalette)
				throw std::runtime_error("voxmap: unknown section kind");

			Section s = Section::dense();
			if (!decodeSection(r.slice(e.offset_, e.size_), e.codec_, remap, s.data()))
				throw std::runtime_error("voxmap: corrupt section payload at index " + std::to_string(i));
			s.tryCollapse();
			world->editSectionAt(0, i) = std::move(s);
		}

		// Trailer is the last 4 bytes.
		if (bytes.size() < 4)
			throw std::runtime_error("voxmap: truncated file");
		Reader t(bytes);
		t.pos_ = bytes.size() - 4;
		if (t.u32() != kTrailer)
			throw std::runtime_error("voxmap: missing trailer - the file was truncated");

		result.world_ = std::move(world);
		return result;
	}
}
