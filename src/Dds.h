#pragma once

namespace Dds
{
	// Decodes a picture (png/jpg/bmp/tga), centre-crops it to a_width:a_height, resizes it,
	// builds the full mip chain and writes a BC1 (DXT1) .dds. Writes to a temp file first and
	// renames, so the game never sees a half-written texture. Returns false and fills a_error on failure.
	bool ConvertPicture(const std::filesystem::path& a_source, int a_width, int a_height,
		const std::filesystem::path& a_out, std::string& a_error);

	// Writes a small flat (128,128,255) normal map, used when the mesh's own canvas normal map is missing.
	bool WriteFlatNormal(const std::filesystem::path& a_out);
}
