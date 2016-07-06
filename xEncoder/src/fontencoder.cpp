#include "fontencoder.h"
#include "XTBfile.h"

namespace xten{

	static void drawBitmap(unsigned char* dstBitmap, int x, int y, int dstWidth, unsigned char* srcBitmap, int srcWidth, int srcHeight)
	{
		// offset dst bitmap by x,y.
		dstBitmap += (x + (y * dstWidth));

		for (int i = 0; i < srcHeight; ++i)
		{
			memcpy(dstBitmap, (const void*)srcBitmap, srcWidth);
			srcBitmap += srcWidth;
			dstBitmap += dstWidth;
		}
	}

	static void writeUint(FILE* fp, unsigned int i)
	{
		fwrite(&i, sizeof(unsigned int), 1, fp);
	}

	static void writeString(FILE* fp, const char* str)
	{
		unsigned int len = strlen(str);
		fwrite(&len, sizeof(unsigned int), 1, fp);
		if (len > 0)
		{
			fwrite(str, 1, len, fp);
		}
	}

	unsigned char* createDistanceFields(unsigned char* img, unsigned int width, unsigned int height)
	{
		short* xDistance = (short*)malloc(width * height * sizeof(short));
		short* yDistance = (short*)malloc(width * height * sizeof(short));
		double* gx = (double*)calloc(width * height, sizeof(double));
		double* gy = (double*)calloc(width * height, sizeof(double));
		double* data = (double*)calloc(width * height, sizeof(double));
		double* outside = (double*)calloc(width * height, sizeof(double));
		double* inside = (double*)calloc(width * height, sizeof(double));
		unsigned int i;

		// Convert img into double (data)
		double imgMin = 255;
		double imgMax = -255;
		for (i = 0; i < width * height; ++i)
		{
			double v = img[i];
			data[i] = v;
			if (v > imgMax)
				imgMax = v;
			if (v < imgMin)
				imgMin = v;
		}
		// Rescale image levels between 0 and 1
		for (i = 0; i < width * height; ++i)
		{
			data[i] = (img[i] - imgMin) / imgMax;
		}
		// Compute outside = edtaa3(bitmap); % Transform background (0's)
		computegradient(data, width, height, gx, gy);
		edtaa3(data, gx, gy, height, width, xDistance, yDistance, outside);
		for (i = 0; i < width * height; ++i)
		{
			if (outside[i] < 0)
				outside[i] = 0.0;
		}
		// Compute inside = edtaa3(1-bitmap); % Transform foreground (1's)
		memset(gx, 0, sizeof(double) * width * height);
		memset(gy, 0, sizeof(double) * width * height);
		for (i = 0; i < width * height; ++i)
		{
			data[i] = 1 - data[i];
		}
		computegradient(data, width, height, gx, gy);
		edtaa3(data, gx, gy, height, width, xDistance, yDistance, inside);
		for (i = 0; i < width * height; ++i)
		{
			if (inside[i] < 0)
				inside[i] = 0.0;
		}
		// distmap = outside - inside; % Bipolar distance field
		unsigned char* out = (unsigned char*)malloc(sizeof(unsigned char) * width * height);
		for (i = 0; i < width * height; ++i)
		{
			outside[i] -= inside[i];
			outside[i] = 128 + outside[i] * 16;
			if (outside[i] < 0)
				outside[i] = 0;
			if (outside[i] > 255)
				outside[i] = 255;
			out[i] = 255 - (unsigned char)outside[i];
		}
		free(xDistance);
		free(yDistance);
		free(gx);
		free(gy);
		free(data);
		free(outside);
		free(inside);

		return out;
	}

	int writeFont(const char* inFilePath, const char* outFilePath, std::vector<unsigned int>& fontSizes, 
		const char* id, bool fontpreview = false, Font::FontFormat fontFormat = Font::BITMAP)
	{

		FT_Library library;
		FT_Error error;

		error = FT_Init_FreeType(&library);

		if (error)
		{
			std::cerr << "FT_Init_FreeType error: " << error << std::endl;
			return -1;
		}

		FT_Face face;
		error = FT_New_Face(library, inFilePath, 0, &face);

		if (error)
		{
			std::cerr << "FT_New_Face error: " << error << std::endl;
			return -1;
		}

		std::vector<FontData*> fonts;

		for (size_t fontIndex = 0, count = fontSizes.size(); fontIndex < count; ++fontIndex)
		{
			unsigned int fontSize = fontSizes[fontIndex];

			FontData * font = XNEW FontData();
			font->fontSize = fontSize;

			TTFGlyph* glyphArray = font->glyphArray;

			int rowSize = 0;
			int glyphSize = 0;
			int actualFontHeight = 0;

			FT_GlyphSlot slot = NULL;
			FT_Int32 loadFlags = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT;

			// We want to generate fonts that fit exactly the requested pixels size.
			// Since free type (due to modern fonts) does not directly correlate requested
			// size to glyph size, we'll brute-force attempt to set the largest font size
			// possible that will fit within the requested pixel size.
			for (unsigned int requestedSize = fontSize; requestedSize > 0; --requestedSize)
			{
				// Set the pixel size.
				error = FT_Set_Char_Size(face, 0, requestedSize * 64, 0, 0);
				if (error)
				{
					std::cerr << "FT_Set_Char_Size error: " << error << std::endl;
					return -1;
				}

				// Save glyph information (slot contains the actual glyph bitmap).
				slot = face->glyph;

				rowSize = 0;
				glyphSize = 0;
				actualFontHeight = 0;

				// Find the width of the image.
				for (unsigned char ascii = START_INDEX; ascii < END_INDEX; ++ascii)
				{
					// Load glyph image into the slot (erase previous one)
					error = FT_Load_Char(face, ascii, loadFlags);
					if (error)
					{
						std::cerr << "FT_Load_Char error: " << error << std::endl;
						return -1;
					}

					int bitmapRows = slot->bitmap.rows;
					actualFontHeight = (actualFontHeight < bitmapRows) ? bitmapRows : actualFontHeight;

					if (slot->bitmap.rows > slot->bitmap_top)
					{
						bitmapRows += (slot->bitmap.rows - slot->bitmap_top);
					}

					rowSize = (rowSize < bitmapRows) ? bitmapRows : rowSize;
				}

				// Have we found a pixel size that fits?
				if (rowSize <= (int)fontSize)
				{
					glyphSize = rowSize;
					rowSize = fontSize;
					break;
				}
			}

			if (slot == NULL || glyphSize == 0)
			{
				std::cerr << "Cannot generate a font of the requested size:" << fontSize << std::endl;
				return -1;
			}

			// Include padding in the rowSize.
			rowSize += GLYPH_PADDING;

			// Initialize with padding.
			int penX = 0;
			int penY = 0;
			int row = 0;

			double powerOf2 = 2;
			unsigned int imageWidth = 0;
			unsigned int imageHeight = 0;
			bool textureSizeFound = false;

			int advance;
			int i;

			while (!textureSizeFound)
			{
				imageWidth = (unsigned int)pow(2.0, powerOf2);
				imageHeight = (unsigned int)pow(2.0, powerOf2);
				penX = 0;
				penY = 0;
				row = 0;

				// Find out the squared texture size that would fit all the require font glyphs.
				i = 0;
				for (unsigned char ascii = START_INDEX; ascii < END_INDEX; ++ascii)
				{
					// Load glyph image into the slot (erase the previous one).
					error = FT_Load_Char(face, ascii, loadFlags);
					if (error)
					{
						std::cerr << "FT_Load_Char error: " << error << std::endl;
					}
					// Glyph image.
					int glyphWidth = slot->bitmap.pitch;
					int glyphHeight = slot->bitmap.rows;

					advance = glyphWidth + GLYPH_PADDING;

					// If we reach the end of the image wrap around to the next row.
					if ((penX + advance) > (int)imageWidth)
					{
						penX = 0;
						row += 1;
						penY = row * rowSize;
						if ((penY + rowSize) > (int)imageHeight)
						{
							powerOf2++;
							break;
						}
					}

					// penY should include the glyph offsets.
					penY += (actualFontHeight - glyphHeight) + (glyphHeight - slot->bitmap_top);

					// Set the pen position for the next glyph
					penX += advance;
					// Move Y back to the top of the row.
					penY = row * rowSize;

					if (ascii == (END_INDEX - 1))
					{
						textureSizeFound = true;
					}
					i++;
				}
			}

			// Try further to find a tighter texture size.
			powerOf2 = 1;
			for (;;)
			{
				if ((penY + rowSize) >= pow(2.0, powerOf2))
				{
					powerOf2++;
				}
				else
				{
					imageHeight = (int)pow(2.0, powerOf2);
					break;
				}
			}

			// Allocate temporary image buffer to draw the glyphs into.
			unsigned char* imageBuffer = (unsigned char*)malloc(imageWidth * imageHeight);
			memset(imageBuffer, 0, imageWidth * imageHeight);
			penX = 1;
			penY = 0;
			row = 0;
			i = 0;
			for (unsigned char ascii = START_INDEX; ascii < END_INDEX; ++ascii)
			{
				// Load glyph image into the slot (erase the previous one).
				error = FT_Load_Char(face, ascii, loadFlags);
				if (error)
				{
					std::cerr << "FT_Load_Char error: " << error << std::endl;
				}

				// Glyph image.
				unsigned char* glyphBuffer = slot->bitmap.buffer;
				int glyphWidth = slot->bitmap.pitch;
				int glyphHeight = slot->bitmap.rows;

				advance = glyphWidth + GLYPH_PADDING;

				// If we reach the end of the image wrap around to the next row.
				if ((penX + advance) > (int)imageWidth)
				{
					penX = 1;
					row += 1;
					penY = row * rowSize;
					if ((penY + rowSize) > (int)imageHeight)
					{
						free(imageBuffer);
						std::cerr << "Image size exceeded! " << error << std::endl;
						return -1;
					}
				}

				// penY should include the glyph offsets.
				penY += (actualFontHeight - glyphHeight) + (glyphHeight - slot->bitmap_top);

				// Draw the glyph to the bitmap with a one pixel padding.
				drawBitmap(imageBuffer, penX, penY, imageWidth, glyphBuffer, glyphWidth, glyphHeight);

				// Move Y back to the top of the row.
				penY = row * rowSize;

				glyphArray[i].index = ascii;
				glyphArray[i].width = advance - GLYPH_PADDING;
				glyphArray[i].bearingX = slot->metrics.horiBearingX >> 6;
				glyphArray[i].advance = slot->metrics.horiAdvance >> 6;

				// Generate UV coords.
				glyphArray[i].uvCoords[0] = (float)penX / (float)imageWidth;
				glyphArray[i].uvCoords[1] = (float)penY / (float)imageHeight;
				glyphArray[i].uvCoords[2] = (float)(penX + advance - GLYPH_PADDING) / (float)imageWidth;
				glyphArray[i].uvCoords[3] = (float)(penY + rowSize - GLYPH_PADDING) / (float)imageHeight;

				// Set the pen position for the next glyph
				penX += advance;
				i++;
			}

			font->glyphSize = glyphSize;
			font->imageBuffer = imageBuffer;
			font->imageWidth = imageWidth;
			font->imageHeight = imageHeight;
			fonts.push_back(font);
		}

		// File header and version.
		FILE *xtenFile = fopen(outFilePath, "wb");
		char fileHeader[9] = { '\xAB', 'G', 'P', 'B', '\xBB', '\r', '\n', '\x1A', '\n' };
		fwrite(fileHeader, sizeof(char), 9, xtenFile);
		fwrite(xten::XTB_VERSION, sizeof(char), 2, xtenFile);

		// Write Ref table (for a single font)
		writeUint(xtenFile, 1);						// Ref[] count
		writeString(xtenFile, id);					// Ref id
		writeUint(xtenFile, 128);					// Ref type
		writeUint(xtenFile, ftell(xtenFile) + 4);	// Ref offset (current pos + 4 bytes)

		// Family name.
		writeString(xtenFile, face->family_name);

		// Style.
		writeUint(xtenFile, 0);

		// Number of included font sizes
		writeUint(xtenFile, (unsigned int)fonts.size());

		for (size_t i = 0, count = fonts.size(); i < count; ++i)
		{
			FontData* font = fonts[i];

			// Font size (pixels).
			writeUint(xtenFile, font->fontSize);

			// Character set. TODO: Empty for now
			writeString(xtenFile, "");

			// Glyphs.
			unsigned int glyphSetSize = END_INDEX - START_INDEX;
			writeUint(xtenFile, glyphSetSize);
			for (unsigned int j = 0; j < glyphSetSize; j++)
			{
				writeUint(xtenFile, font->glyphArray[j].index);
				writeUint(xtenFile, font->glyphArray[j].width);
				fwrite(&font->glyphArray[j].bearingX, sizeof(int), 1, xtenFile);
				writeUint(xtenFile, font->glyphArray[j].advance);
				fwrite(&font->glyphArray[j].uvCoords, sizeof(float), 4, xtenFile);
			}

			// Image dimensions
			unsigned int imageSize = font->imageWidth * font->imageHeight;
			writeUint(xtenFile, font->imageWidth);
			writeUint(xtenFile, font->imageHeight);
			writeUint(xtenFile, imageSize);

			FILE* previewFp = NULL;
			std::string xtenFilePath;
			std::string pgmFilePath;
			if (fontpreview)
			{
				// Save out a pgm monochome image file for preview
				std::ostringstream pgmFilePathStream;
				pgmFilePathStream << getFilenameNoExt(outFilePath) << "-" << font->fontSize << ".pgm";
				pgmFilePath = pgmFilePathStream.str();
				previewFp = fopen(pgmFilePath.c_str(), "wb");
				fprintf(previewFp, "P5 %u %u 255\n", font->imageWidth, font->imageHeight);
			}

			if (fontFormat == Font::DISTANCE_FIELD)
			{
				// Flip height and width since the distance field map generator is column-wise.
				unsigned char* distanceFieldBuffer = createDistanceFields(font->imageBuffer, font->imageHeight, font->imageWidth);

				fwrite(distanceFieldBuffer, sizeof(unsigned char), imageSize, xtenFile);
				writeUint(xtenFile, Font::DISTANCE_FIELD);

				if (previewFp)
				{
					fwrite((const char*)distanceFieldBuffer, sizeof(unsigned char), imageSize, previewFp);
					fclose(previewFp);
				}

				free(distanceFieldBuffer);
			}
			else
			{
				fwrite(font->imageBuffer, sizeof(unsigned char), imageSize, xtenFile);
				writeUint(xtenFile, Font::BITMAP);

				if (previewFp)
				{
					fwrite((const char*)font->imageBuffer, sizeof(unsigned char), font->imageWidth * font->imageHeight, previewFp);
					fclose(previewFp);
				}
			}

			if (previewFp)
			{
				fclose(previewFp);
				std::cout << getBaseName(pgmFilePath) << ".pgm preview image created successfully." << std::endl;
			}
		}

		// Close file.
		fclose(xtenFile);

		std::cout << getBaseName(outFilePath) << ".xtb created successfully." << std::endl;

		XDEL_VEC(fonts);

		FT_Done_Face(face);
		FT_Done_FreeType(library);

		return 0;
	}
}