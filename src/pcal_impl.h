#ifndef _PCAL_IMPL_H
#define _PCAL_IMPL_H
/********************************************************************************************************
 * @file PCal.h
 * Multi-tone Phase Cal Extraction
 *
 * @brief Extracts and integrates multi-tone phase calibration signal information from an input signal.
 *
 * The principle relies on the fact that with a comb spacing of say 1 MHz and a sampling rate of say
 * 32 MHz the single 1 MHz and also all of its multiples (1, 2, .. 16 MHz in the band) have at least
 * one full sine period in 32MHz/1MHz = 32 samples. For extraction and time integration, we simply
 * have to segment the input signal into 32-sample pieces (in the above example) and integrate these
 * pieces.
 *
 * A tiny FFT performed on the integrated 32-bin result gives you the amplitude and phase
 * of every tone. As the PCal amplitude is in practice constant over a short frequency band,
 * the amplitude and phase info after the FFT directly gives you the equipment filter response.
 *
 * The extracted PCal can also be analyzed in the time domain (no FFT). The relative, average instrumental
 * delay time can be found directly by estimating the position of the peak in the time-domain data.
 *
 * @author   Jan Wagner
 * @author   Sergei Pogrebenko
 * @author   Walter Brisken
 * @version  1.1/2009
 * @license  GNU GPL v3
 *
 * Changelog:
 *   05Oct2009 - added support for arbitrary input segment lengths
 *   08oct2009 - added Briskens rotationless method 
 *
 ********************************************************************************************************/

#include "architecture.h"
#include <cstddef>
#include <stdint.h>
using std::size_t;

class pcal_config_pimpl;

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// DERIVED CLASS: extraction of zero-offset PCal signals
/////////////////////////////////////////////////////////////////////////////////////////////////////////

class PCalExtractorTrivial : public PCal {
   public:
      PCalExtractorTrivial(double bandwidth_hz, double pcal_spacing_hz);
      ~PCalExtractorTrivial();

   public:
      /**
       * Set the extracted and accumulated PCal data back to zero.
       */
      void clear();

      /**
       * Extracts multi-tone PCal information from a single-channel signal segment
       * and integrates it to the class-internal PCal extraction result buffer.
       * There are no restrictions to the segment length.
       *
       * If you integrate over a longer time and several segments, i.e. perform
       * multiple calls to this function, take care to keep the input
       * continuous (i.e. don't leave out samples).
       *
       * If extraction has been finalized by calling getFinalPCal() this function
       * returns False. You need to call clear() to reset.
       *
       * @paran samples Chunk of the input signal consisting of 'float' samples
       * @param len     Length of the input signal chunk
       * @return true on success
       */
      bool extractAndIntegrate(f32 const* samples, const size_t len);

      /**
       * Performs finalization steps on the internal PCal results if necessary
       * and then copies these PCal results into the specified output array.
       * Data in the output array is overwritten with PCal results.
       *
       * @param pointer to user PCal array with getLength() values
       */
      void getFinalPCal(cf32* out);
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////
// DERIVED CLASS: extraction of PCal signals with non-zero offset
/////////////////////////////////////////////////////////////////////////////////////////////////////////

class PCalExtractorShifting : public PCal {
   public:
      PCalExtractorShifting(double bandwidth_hz, double pcal_spacing_hz, double pcal_offset_hz);
      ~PCalExtractorShifting();

   public:
      /**
       * Set the extracted and accumulated PCal data back to zero.
       */
      void clear();

      /**
       * Extracts multi-tone PCal information from a single-channel signal segment
       * and integrates it to the class-internal PCal extraction result buffer.
       * There are no restrictions to the segment length.
       *
       * If you integrate over a longer time and several segments, i.e. perform
       * multiple calls to this function, take care to keep the input
       * continuous (i.e. don't leave out samples).
       *
       * If extraction has been finalized by calling getFinalPCal() this function
       * returns False. You need to call clear() to reset.
       *
       * @paran samples Chunk of the input signal consisting of 'float' samples
       * @param len     Length of the input signal chunk
       * @return true on success
       */
      bool extractAndIntegrate(f32 const* samples, const size_t len);

      /**
       * Performs finalization steps on the internal PCal results if necessary
       * and then copies these PCal results into the specified output array.
       * Data in the output array is overwritten with PCal results.
       *
       * @param pointer to user PCal array with getLength() values
       */
      void getFinalPCal(cf32* out);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// DERIVED CLASS: extraction of PCal signals with non-zero offset and FFT-implicit rotation possible
/////////////////////////////////////////////////////////////////////////////////////////////////////////

class PCalExtractorImplicitShift : public PCal {
   public:
      PCalExtractorImplicitShift(double bandwidth_hz, double pcal_spacing_hz, double pcal_offset_hz);
      ~PCalExtractorImplicitShift();

   public:
      /**
       * Set the extracted and accumulated PCal data back to zero.
       */
      void clear();

      /**
       * Extracts multi-tone PCal information from a single-channel signal segment
       * and integrates it to the class-internal PCal extraction result buffer.
       * There are no restrictions to the segment length.
       *
       * If you integrate over a longer time and several segments, i.e. perform
       * multiple calls to this function, take care to keep the input
       * continuous (i.e. don't leave out samples).
       *
       * If extraction has been finalized by calling getFinalPCal() this function
       * returns False. You need to call clear() to reset.
       *
       * @paran samples Chunk of the input signal consisting of 'float' samples
       * @param len     Length of the input signal chunk
       * @return true on success
       */
      bool extractAndIntegrate(f32 const* samples, const size_t len);

      /**
       * Performs finalization steps on the internal PCal results if necessary
       * and then copies these PCal results into the specified output array.
       * Data in the output array is overwritten with PCal results.
       *
       * @param pointer to user PCal array with getLength() values
       */
      void getFinalPCal(cf32* out);
};

#endif // _PCAL_IMPL_H

