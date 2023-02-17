// -*- tab-width: 4; -*-
// vi: set sw=2 ts=4 expandtab:

// Copyright 2022 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0

//!
//! @internal
//! @~English
//! @file imageio.h
//!
//! @brief Base classes for image input and output plugins
//!
//! The API for these classes is inspired by that of OpenImageIO. We don't use
//! OIIO because
//! - the total size, with all its dependencies, is 128 Mb. There is no easy
//!   way, i.e. via cmake comnfiguration, to omit plugins that are of no
//!   interest.
//! - it takes between 40m and 1hr on the CI services to build it and all
//!   its dependencies
//! - I have consistently been unable to build the vcpkg version of it for
//!   all the platforms we need. vcpkg is the only package manager whose
//!   installed products are redistributable.
//!

#pragma once

#include "stdafx.h"

#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <sstream>
#include <vector>

#include <fmt/format.h>
#include <math.h>

#include "formatdesc.h"

using stride_t = int64_t;
const stride_t AutoStride = std::numeric_limits<stride_t>::min();

typedef bool (*ProgressCallback)(void *opaque_data, float portion_done);

class ImageSpec {
  protected:
    FormatDescriptor formatDesc;
    uint32_t imageWidth;           ///< width of the pixel data
    uint32_t imageHeight;          ///< height of the pixel data
    uint32_t imageDepth;           ///< depth of pixel data, >1 indicates a "volume"

  public:
    ImageSpec() : imageWidth(0), imageHeight(0), imageDepth(0) { }

    ImageSpec(uint32_t w, uint32_t h, uint32_t d,
               uint32_t channelCount, uint32_t channelBitCount,
               khr_df_sample_datatype_qualifiers_e dt
                  = static_cast<khr_df_sample_datatype_qualifiers_e>(0),
               khr_df_transfer_e t = KHR_DF_TRANSFER_UNSPECIFIED,
               khr_df_primaries_e p = KHR_DF_PRIMARIES_BT709,
               khr_df_model_e m = KHR_DF_MODEL_RGBSDA,
               khr_df_flags_e f = KHR_DF_FLAG_ALPHA_STRAIGHT)
        : formatDesc(channelCount, channelBitCount, dt, t, p, m, f),
          imageWidth(w), imageHeight(h), imageDepth(d) { }

    ImageSpec(uint32_t w, uint32_t h, uint32_t d,
               uint32_t channelCount, uint32_t channelBitCount,
               uint32_t channelLower, uint32_t channelUpper,
               khr_df_sample_datatype_qualifiers_e dt
                  = static_cast<khr_df_sample_datatype_qualifiers_e>(0),
               khr_df_transfer_e t = KHR_DF_TRANSFER_UNSPECIFIED,
               khr_df_primaries_e p = KHR_DF_PRIMARIES_BT709,
               khr_df_model_e m = KHR_DF_MODEL_RGBSDA,
               khr_df_flags_e f = KHR_DF_FLAG_ALPHA_STRAIGHT)
        : formatDesc(channelCount, channelBitCount,
                     channelLower, channelUpper,
                     dt, t, p, m, f),
          imageWidth(w), imageHeight(h), imageDepth(d) { }

    ImageSpec(uint32_t w, uint32_t h, uint32_t d,
               uint32_t channelCount, std::vector<uint>& channelBitLengths,
               std::vector<khr_df_model_channels_e>& channelTypes,
               khr_df_sample_datatype_qualifiers_e dt
                  = static_cast<khr_df_sample_datatype_qualifiers_e>(0),
               khr_df_transfer_e t = KHR_DF_TRANSFER_UNSPECIFIED,
               khr_df_primaries_e p = KHR_DF_PRIMARIES_BT709,
               khr_df_model_e m = KHR_DF_MODEL_RGBSDA,
               khr_df_flags_e f = KHR_DF_FLAG_ALPHA_STRAIGHT)
        : formatDesc(channelCount, channelBitLengths,
                     channelTypes, dt, t, p, m, f),
          imageWidth(w), imageHeight(h), imageDepth(d) { }


    FormatDescriptor& format() { return formatDesc; }
    const FormatDescriptor& format() const { return formatDesc; }

    uint32_t width() const noexcept { return imageWidth; }
    uint32_t height() const noexcept { return imageHeight; }
    uint32_t depth() const noexcept { return imageDepth; }

    void setWidth(uint32_t w) { imageWidth = w; }
    void setHeight(uint32_t h) { imageHeight = h; }
    void setDepth(uint32_t d) { imageDepth = d; }

    size_t imagePixelCount() const noexcept {
        return depth() * width() * height();
    };

    size_t imageChannelCount() const noexcept  {
        return imagePixelCount() * format().channelCount();
    };

    size_t imageByteCount() const noexcept  {
        return imagePixelCount() * format().pixelByteCount();
    };

    size_t scanlineByteCount() const noexcept  {
        return width() * format().pixelByteCount();
    }

    size_t scanlineChannelCount() const noexcept  {
        return width() * format().channelCount();
    }
};

typedef std::function<void(const std::string&)> WarningCallbackFunction;

class ImageInput {
  protected:
    std::ifstream file;
    std::unique_ptr<std::stringstream> buffer;
    std::istream* isp = nullptr;
    std::string name;
    _tstring _filename;
    std::vector<uint16_t> nativeBuffer16;
    std::vector<uint8_t> nativeBuffer8;
    struct imageInfo {
        ImageSpec spec;
        size_t filepos;

        imageInfo(ImageSpec&& is, size_t pos = 0)
            : spec(is), filepos(pos) { }
    };
    std::vector<imageInfo> images;                                 ///<
    uint curSubimage = std::numeric_limits<int>::max();
    uint curMiplevel = std::numeric_limits<int>::max();
    WarningCallbackFunction sendWarning = nullptr;

  public:
    using unique_ptr = std::unique_ptr<ImageInput>;

    /// @brief Create an ImageInput subclass instance that is able to read the
    /// given file and open it.
    ///
    /// The `config`, if not nullptr, points to an ImageSpec giving hints,
    /// requests, or special instructions.  ImageInput implementations are
    /// free to not respond to any such requests, so the default
    /// implementation is just to ignore `config`.
    ///
    /// `open()` will first try to make an ImageInput corresponding to
    /// the format implied by the file extension (for example, `"foo.tif"`
    /// will try the TIFF plugin), but if one is not found or if the
    /// inferred one does not open the file, every known ImageInput type
    /// will be tried until one is found that will open the file.
    ///
    /// @param[in] filename  The name of the file to open.
    ///
    /// @param[in] config    Optional pointer to an ImageSpec whose metadata
    ///                     contains "configuration hints."
    ///
    /// @returns
    ///        A `unique_ptr` that will close and free the ImageInput when
    ///        it exits scope or is reset. The pointer will be empty if the
    ///        required writer was not able to be created. If the open fails,
    ///        the `unique_ptr` will be empty. An error can be retrieved by
    ///        ImageInput::geterror().
    static unique_ptr open(const _tstring& filename,
                           const ImageSpec *config=nullptr,
                           WarningCallbackFunction wcb=nullptr);
                           //Filesystem::IOProxy* ioproxy = nullptr,
                           //string_view plugin_searchpath="");

    virtual ~ImageInput() { close(); }

    // TODO: is config necessary?
    virtual void open (const _tstring& filename, ImageSpec& newspec);
    virtual void open (const _tstring& filename, ImageSpec& newspec,
                       const ImageSpec& /*config*/) {
        return open(filename, newspec);
    }

    virtual void close() {
        if (isp == buffer.get()) {
            buffer.reset();
        } else if (file.is_open()) {
            file.close();
        }
        isp = nullptr;
    }

    void connectCallback(WarningCallbackFunction wcb) {
        sendWarning = wcb;
    }

  protected:
    ImageInput(std::string&& name) : name(name) { }

    virtual void open (std::ifstream&& ifs, ImageSpec& newspec) {
        file = std::move(ifs);
        isp = &file;
        open(newspec);
    }
    virtual void open (std::ifstream& ifs, ImageSpec& newspec,
                       const ImageSpec& /*config*/) {
        return open(ifs, newspec);
    }

    virtual void open (std::unique_ptr<std::stringstream>&& iss,
                       ImageSpec& newspec) {
        buffer = std::move(iss);
        isp = buffer.get();
        open(newspec);
    }
    virtual void open (std::unique_ptr<std::stringstream>&& iss,
                       ImageSpec& newspec,
                       const ImageSpec& /*config*/) {
        return open(std::move(iss), newspec);
    }

    virtual void open (std::istream& cin, ImageSpec& newspec) {
        isp = &cin;
        open(newspec);
    }
    virtual void open (std::istream& cin, ImageSpec& newspec,
                       const ImageSpec& /*config*/) {
        return open(cin, newspec);
    }

    virtual void open(ImageSpec& newspec) = 0;
    //virtual void open(ImageSpec& newspec, const ImageSpec& config) = 0;

    std::ifstream& getFile() { return file; }
    std::unique_ptr<std::stringstream>& getBuffer() { return buffer; }

    void throwOnReadFailure();

    void warning(const std::string& wmsg);
    void fwarning(const std::string& wmsg);

  private:
    /** @internal
     * @brief Open file or stringstream.
     *
     * This is solely to support the static open(). Subclasses should have no need
     * to override.
     *
     * std::move is used because if the open is successful the class object needs to retain the
     * fstream or stringstream that would otherwise disappear when open() exits. The catch
     * clauses std::move the stream info back to the caller so it can keep searching for a plugin.
     */
    void open(const _tstring& filename, std::ifstream& ifs,
              std::unique_ptr<std::stringstream>& buffer,
              ImageSpec& newspec) {
        _filename = filename;    // Purely so warnings can include the file name.
        if (ifs.is_open()) {
            try {
                open(std::move(ifs), newspec);
            } catch (...) {
                ifs = std::move(getFile());
                ifs.seekg(0);
                throw;
            }
        } else if (buffer.get() != nullptr) {
            try {
                open(std::move(buffer), newspec);
            } catch (...) {
                buffer = std::move(getBuffer());
                buffer.get()->seekg(0);
                throw;
            }
        } else {
            try {
                open(std::cin, newspec);
            } catch (...) {
                std::cin.seekg(0);
                throw;
            }
        }
    }
    void open(const _tstring& fname,
              std::ifstream& ifs,
              std::unique_ptr<std::stringstream>& buffer,
              ImageSpec& newspec,
              const ImageSpec& /*config*/) {
        open(fname, ifs, buffer, newspec);
    }

  public:
    virtual const std::string& formatName(void) const { return name; }
    virtual const _tstring& filename(void) const { return _filename; }

    // Return a reference to the ImageSpec of the current image.
    // This default method assumes no subimages.
    virtual const ImageSpec& spec (void) const {
        return images[0].spec;
    }

    // Return a full copy of the ImageSpec of the designated subimage & level.
    // If there is no such subimage and miplevel it returns an ImageSpec
    // whose format returns true for isUnknown().
    // This default method assumes no subimages.
    virtual ImageSpec spec (uint /*subimage*/, uint /*miplevel=0*/) {
        ImageSpec ret;
        if (curSubimage < images.size()) {
            ret = images[curSubimage].spec;
        }
        return ret;
    }

    // Return a copy of the ImageSpec but only the dimension and type fields.
    // TODO: Determine if this is necessary.
    virtual ImageSpec spec_dimensions (uint /*subimage*/, uint /*miplevel=0*/) {
        return spec();
    }

    virtual uint currentSubimage(void) const { return curSubimage; }
    virtual uint currentMiplevel(void) const { return curMiplevel; }
    virtual uint subimageCount(void) const { return 1; }
    virtual uint miplevelCount(void) const { return 1; }
    virtual bool seekSubimage(uint subimage, uint miplevel = 0) {
        // Default implementation assumes no support for subimages or
        // mipmaps, so there is no work to do.
        return subimage == currentSubimage() && miplevel == currentMiplevel();
    }

    /// Read an entire image into contiguous memory performing conversions to
    /// @a targetFormat.
    ///
    // @TODO @a targetFormat allows callers to request almost unlimited
    // possible conversions compared to the original format. The current
    // plug-ins only provide a handful of conversions and those available
    // vary by plug-in. As a work in progress this is okay but we need to
    // rationalize all this such as
    //
    //   1. a subset of all possible conversions supported by every plug-in
    //   2. a way to query the available conversions and specify one or more.
    //
    // Currently supported transformations are:
    //   @b Npbm can do bits/pixel rescaling, including 16- to 8-bit
    //   conversions.
    //   @b png can also do bits/pixel rescaling, including 16- to 8-bit.
    //   @b Jpeg can convert the number of components. This is not used by
    //   callers as, for historic reasons, this conversion is provided by
    //   the upper level Image class.
    virtual void readImage(void* buffer, size_t bufferByteCount,
                           uint subimage = 0, uint miplevel = 0,
                           const FormatDescriptor& targetFormat = FormatDescriptor());

    /// Read a scanline into contiguous performing conversions to
    /// @a targetFormat.
    ///
    /// @sa See readImage for information about handling of targetFormat.
    virtual void readScanline(void* buffer, size_t bufferByteCount,
                              uint y, uint z,
                              uint subimage, uint miplevel,
                              const FormatDescriptor& targetFormat = FormatDescriptor());
    /// Read a single scanline (all channels) of native data into contiguous
    /// memory.
    virtual void readNativeScanline(void* buffer, size_t bufferByteCount,
                                    uint y, uint z = 0,
                                    uint subimage = 0, uint miplevel = 0) = 0;

    template<class Tr, class Tw>
    inline static void
    rescale(Tw* write, const Tr* read, size_t nvals, Tr max)
    {
        if (max) {
            float multiplier = std::numeric_limits<Tw>::max() / max;
            for (size_t i = 0; i < nvals; i++) {
                write[i] = static_cast<Tw>(roundf(read[i] * multiplier));
            }
        }
    }

    template<class Tr, class Tw>
    inline static void
    rescale(Tw* write, Tw maxw, const Tr* read, Tr maxr, size_t nvals)
    {
        if (maxr) {
            float multiplier = maxw / maxr;
            for (size_t i = 0; i < nvals; i++) {
                write[i] = static_cast<Tw>(roundf(read[i] * multiplier));
            }
        }
    }

    class different_format : public std::runtime_error {
      public:
        different_format() : std::runtime_error("") { }
    };
    class invalid_file : public std::runtime_error {
      public:
        invalid_file(std::string error)
            : std::runtime_error("Invalid file: " + error) { }
    };
    class buffer_too_small : public std::runtime_error {
      public:
        buffer_too_small() : std::runtime_error("Image buffer too small.") { }
    };

    typedef ImageInput* (*Creator)();
};


class ImageOutput {
public:
    /// unique_ptr to an ImageOutput.
    using unique_ptr = std::unique_ptr<ImageOutput>;

    static unique_ptr create (const _tstring& name);

protected:
    ImageOutput(std::string&& name) : name(name) { }

public:
    virtual ~ImageOutput () { };

    /// Return the name of the format implemented by this class.
    virtual const std::string& formatName(void) const { return name; }

    /// Query if feature is supported.
    virtual int supports (std::string /*feature*/) const {
        return false;
    }

    /// Modes passed to the `open()` call.
    enum OpenMode { Create, AppendSubimage, AppendMIPLevel };

    ///
    /// @param  name        The name of the image file to open.
    /// @param  newspec     The ImageSpec describing the resolution, data
    ///                     types, etc.
    /// @param  mode        Specifies whether the purpose of the `open` is
    ///                     to create/truncate the file (default: `Create`),
    ///                     append another subimage (`AppendSubimage`), or
    ///                     append another MIP level (`AppendMIPLevel`).
    /// @returns            `true` upon success, or `false` upon failure.
    virtual void open (const _tstring& name, const ImageSpec& newspec,
                       OpenMode mode=Create) = 0;

    /// Return a reference to the image format specification of the current
    /// subimage.  Note that the contents of the spec are invalid before
    /// `open()` or after `close()`.
    const ImageSpec &spec (void) const { return imageSpec; }

    /// Closes the currently open file associated with this ImageOutput and
    /// frees any memory or resources associated with it.
    virtual void close () = 0;

   ///
    /// @param  y/z         The y & z coordinates of the scanline.
    /// @param  format      A FormatDescriptor describing @a data.
    /// @param  data        Pointer to the pixel data.
    /// @param  xstride     The distance in bytes between successive
    ///                     pixels in @a data (or `AutoStride`).
    virtual void writeScanline (int y, int z, const FormatDescriptor& format,
                                const void *data, stride_t xstride=AutoStride);


    ///
    /// @param  format      A FormatDescriptor describing @a data.
    /// @param  data        Pointer to the pixel data.
    /// @param  xstride/ystride/zstride
    ///                     The distance in bytes between successive pixels,
    ///                     scanlines, and image planes (or `AutoStride`).
    /// @param  progress_callback/progress_callback_data
    ///                     Optional progress callback.
    /// @returns            `true` upon success, or `false` upon failure.
    virtual void writeImage (const FormatDescriptor& format, const void *data,
                             stride_t xstride=AutoStride,
                             stride_t ystride=AutoStride,
                             stride_t zstride=AutoStride,
                             ProgressCallback progress_callback=nullptr,
                             void *progress_callback_data=nullptr);

    /// Specify a reduced-resolution ("thumbnail") version of the image.
    /// Note that many image formats may require the thumbnail to be
    /// specified prior to writing the pixels.
    ///
    //virtual void setThumbnail(const Image& thumb) { return false; }

    /// Read the pixels of the current subimage of  @a in, and write it as the
    /// next subimage of `*this`, in a way that is efficient and does not
    /// alter pixel values, if at all possible.  Both  @a in and `this` must
    /// be a properly-opened `ImageInput and `ImageOutput`, respectively,
    /// and their current images must match in size and number of channels.
    ///
    /// If a particular ImageOutput implementation does not supply a
    /// `copy_image` method, it will inherit the default implementation,
    /// which is to simply read scanlines from @a in and write them
    /// to `*this`.
    ///
    /// @param  in          A pointer to the open `ImageInput` to read from.
    virtual void copyImage (ImageInput *in);

    /// Call signature of a function that creates and returns an
    /// `ImageOutput*`.
    typedef ImageOutput* (*Creator)();

protected:
    ImageSpec imageSpec;           ///< format spec of the currently open image
    std::string name;

};

namespace Imageio {

class string : public std::string {
  public:
    using std::string::string;
    string tolower() {
        std::transform(begin(), end(), begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return *this;
    }

    string (const std::string& s) : std::string(s) {}
};

typedef std::map<std::string, ImageInput::Creator> InputPluginMap;
extern InputPluginMap inputFormats;
extern InputPluginMap inputExtensions;

typedef std::map<std::string, ImageOutput::Creator> OutputPluginMap;
extern OutputPluginMap outputFormats;
extern OutputPluginMap outputExtensions;

void catalogBuiltinPlugins();
}

