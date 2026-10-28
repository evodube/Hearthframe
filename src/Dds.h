#pragma once

namespace Dds
{
	struct ReadResult
	{
		std::vector<std::uint8_t> bytes;
		std::string               error;      // empty on success
		bool                      notFound = false;  // the file was listed but can't be opened by name (MO2/USVFS)
	};

	// Reads a whole file, sharing read/write/delete with other programs (a picture still being saved
	// can be read), retrying a few times while it is locked or empty. Thread-safe.
	ReadResult ReadPictureFile(const std::filesystem::path& a_path);

	// Decodes a picture file to RGBA8 with stb_image. Returns nullptr and fills a_error on failure;
	// free the result with FreePixels().
	unsigned char* DecodePicture(const std::filesystem::path& a_path, int& a_w, int& a_h, std::string& a_error, bool& a_notFound);
	void           FreePixels(unsigned char* a_px);

	// Decodes a picture (png/jpg/bmp/tga), centre-crops it to a_width:a_height, resizes it,
	// builds the full mip chain and writes a BC1 (DXT1) .dds. Writes to a temp file first and
	// renames, so the game never sees a half-written texture. Returns false and fills a_error on failure.
	bool ConvertPicture(const std::filesystem::path& a_source, int a_width, int a_height,
		const std::filesystem::path& a_out, std::string& a_error);

	// Writes a small flat (128,128,255) normal map, used when the mesh's own canvas normal map is missing.
	bool WriteFlatNormal(const std::filesystem::path& a_out);
}
