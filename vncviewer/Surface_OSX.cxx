/* Copyright 2016 Pierre Ossman for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>

#include <stdexcept>

#include <ApplicationServices/ApplicationServices.h>

#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>

#include "cocoa.h"
#include "Surface.h"

static CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

static CGImageRef create_image(CGColorSpaceRef lut,
                               const unsigned char* data,
                               int w, int h, bool skip_alpha)
{
  CGDataProviderRef provider;
  CGImageAlphaInfo alpha;

  CGImageRef image;

  provider = CGDataProviderCreateWithData(nullptr, data,
                                          w * h * 4, nullptr);
  if (!provider)
    throw std::runtime_error("CGDataProviderCreateWithData");

  // FIXME: This causes a performance hit, but is necessary to avoid
  //        artifacts in the edges of the window
  if (skip_alpha)
    alpha = kCGImageAlphaNoneSkipFirst;
  else
    alpha = kCGImageAlphaPremultipliedFirst;

  image = CGImageCreate(w, h, 8, 32, w * 4, lut,
                        alpha | kCGBitmapByteOrder32Little,
                        provider, nullptr, false,
                        kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  if (!image)
    throw std::runtime_error("CGImageCreate");

  return image;
}

static void render(CGContextRef gc, CGColorSpaceRef lut,
                   const unsigned char* data,
                   CGBlendMode mode, CGFloat alpha,
                   int src_w, int src_h, int scaled_w, int scaled_h,
                   int src_x, int src_y,
                   int x, int y, int w, int h)
{
  CGRect rect;
  CGImageRef image, subimage;

  image = create_image(lut, data, src_w, src_h, mode == kCGBlendModeCopy);

  CGContextSaveGState(gc);

  CGContextSetBlendMode(gc, mode);
  CGContextSetAlpha(gc, alpha);

  if ((scaled_w == src_w) && (scaled_h == src_h)) {
    rect.origin.x = src_x;
    rect.origin.y = src_y;
    rect.size.width = w;
    rect.size.height = h;

    subimage = CGImageCreateWithImageInRect(image, rect);
    if (!subimage) {
      CGContextRestoreGState(gc);
      CGImageRelease(image);
      throw std::runtime_error("CGImageCreateImageWithImageInRect");
    }

    rect.origin.x = x;
    rect.origin.y = y;
    rect.size.width = w;
    rect.size.height = h;

    CGContextDrawImage(gc, rect, subimage);

    CGImageRelease(subimage);
  } else {
    // Core Graphics cannot stretch a sub rectangle of the image
    // without introducing rounding errors along the edges, so stretch
    // the entire image and let the clip region pick out the part we
    // are actually interested in
    rect.origin.x = x;
    rect.origin.y = y;
    rect.size.width = w;
    rect.size.height = h;

    CGContextClipToRect(gc, rect);

    CGContextSetInterpolationQuality(gc, kCGInterpolationHigh);

    // Note that the y axis is flipped compared to the source
    // coordinates we've been given
    rect.origin.x = x - src_x;
    rect.origin.y = y + h + src_y - scaled_h;
    rect.size.width = scaled_w;
    rect.size.height = scaled_h;

    CGContextDrawImage(gc, rect, image);
  }

  CGContextRestoreGState(gc);

  CGImageRelease(image);
}

static CGContextRef make_bitmap(int width, int height, unsigned char* data)
{
  CGContextRef bitmap;

  bitmap = CGBitmapContextCreate(data, width, height, 8, width*4, srgb,
                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  if (!bitmap)
    throw std::runtime_error("CGBitmapContextCreate");

  return bitmap;
}

void Surface::clear(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  unsigned char* out;
  int x, y;

  r = (unsigned)r * a / 255;
  g = (unsigned)g * a / 255;
  b = (unsigned)b * a / 255;

  out = data;
  for (y = 0;y < width();y++) {
    for (x = 0;x < height();x++) {
      *out++ = b;
      *out++ = g;
      *out++ = r;
      *out++ = a;
    }
  }
}

void Surface::draw(int src_x, int src_y, int dst_x, int dst_y,
                   int dst_w, int dst_h)
{
  draw(width(), height(), src_x, src_y, dst_x, dst_y, dst_w, dst_h);
}

void Surface::draw(Surface* dst, int src_x, int src_y,
                   int dst_x, int dst_y, int dst_w, int dst_h)
{
  draw(dst, width(), height(), src_x, src_y, dst_x, dst_y, dst_w, dst_h);
}

void Surface::draw(int scaled_w, int scaled_h,
                   int src_x, int src_y, int dst_x, int dst_y,
                   int dst_w, int dst_h)
{
  CGColorSpaceRef lut;

  CGContextSaveGState(fl_gc);

  // Reset the transformation matrix back to the default identity
  // matrix as otherwise we get a massive performance hit
  CGContextConcatCTM(fl_gc, CGAffineTransformInvert(CGContextGetCTM(fl_gc)));

  // macOS Coordinates are from bottom left, not top left
  dst_y = Fl_Window::current()->h() - (dst_y + dst_h);

  lut = cocoa_win_color_space(Fl_Window::current());
  render(fl_gc, lut, data, kCGBlendModeCopy, 1.0,
         width(), height(), scaled_w, scaled_h,
         src_x, src_y, dst_x, dst_y, dst_w, dst_h);
  CGColorSpaceRelease(lut);

  CGContextRestoreGState(fl_gc);
}

void Surface::draw(Surface* dst, int scaled_w, int scaled_h,
                   int src_x, int src_y, int dst_x, int dst_y,
                   int dst_w, int dst_h)
{
  CGContextRef bitmap;

  bitmap = make_bitmap(dst->width(), dst->height(), dst->data);

  // macOS Coordinates are from bottom left, not top left
  dst_y = dst->height() - (dst_y + dst_h);

  render(bitmap, srgb, data, kCGBlendModeCopy, 1.0,
         width(), height(), scaled_w, scaled_h,
         src_x, src_y, dst_x, dst_y, dst_w, dst_h);

  CGContextRelease(bitmap);
}

void Surface::blend(int src_x, int src_y, int dst_x, int dst_y,
                    int dst_w, int dst_h, int a)
{
  CGColorSpaceRef lut;

  CGContextSaveGState(fl_gc);

  // Reset the transformation matrix back to the default identity
  // matrix as otherwise we get a massive performance hit
  CGContextConcatCTM(fl_gc, CGAffineTransformInvert(CGContextGetCTM(fl_gc)));

  // macOS Coordinates are from bottom left, not top left
  dst_y = Fl_Window::current()->h() - (dst_y + dst_h);

  lut = cocoa_win_color_space(Fl_Window::current());
  render(fl_gc, lut, data, kCGBlendModeNormal, (CGFloat)a/255.0,
         width(), height(), width(), height(),
         src_x, src_y, dst_x, dst_y, dst_w, dst_h);
  CGColorSpaceRelease(lut);

  CGContextRestoreGState(fl_gc);
}

void Surface::blend(Surface* dst, int src_x, int src_y,
                    int dst_x, int dst_y, int dst_w, int dst_h, int a)
{
  CGContextRef bitmap;

  bitmap = make_bitmap(dst->width(), dst->height(), dst->data);

  // macOS Coordinates are from bottom left, not top left
  dst_y = dst->height() - (dst_y + dst_h);

  render(bitmap, srgb, data, kCGBlendModeNormal, (CGFloat)a/255.0,
         width(), height(), width(), height(),
         src_x, src_y, dst_x, dst_y, dst_w, dst_h);

  CGContextRelease(bitmap);
}

void Surface::alloc()
{
  data = new unsigned char[width() * height() * 4];
}

void Surface::dealloc()
{
  delete [] data;
}

void Surface::update(const Fl_RGB_Image* image)
{
  int x, y;
  const unsigned char* in;
  unsigned char* out;

  assert(image->w() == width());
  assert(image->h() == height());

  // Convert data and pre-multiply alpha
  in = (const unsigned char*)image->data()[0];
  out = data;
  for (y = 0;y < image->h();y++) {
    for (x = 0;x < image->w();x++) {
      switch (image->d()) {
      case 1:
        *out++ = in[0];
        *out++ = in[0];
        *out++ = in[0];
        *out++ = 0xff;
        break;
      case 2:
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = (unsigned)in[0] * in[1] / 255;
        *out++ = in[1];
        break;
      case 3:
        *out++ = in[2];
        *out++ = in[1];
        *out++ = in[0];
        *out++ = 0xff;
        break;
      case 4:
        *out++ = (unsigned)in[2] * in[3] / 255;
        *out++ = (unsigned)in[1] * in[3] / 255;
        *out++ = (unsigned)in[0] * in[3] / 255;
        *out++ = in[3];
        break;
      }
      in += image->d();
    }
    if (image->ld() != 0)
      in += image->ld() - image->w() * image->d();
  }
}
