// Copyright (C) 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1999-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  unistr_case.cpp
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:2
*
*   created on: 2004aug19
*   created by: Markus W. Scherer
*
*   Case-mapping functions moved here from unistr.cpp
*/

#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "cstring.h"
#include "cmemory.h"
#include "unicode/ustring.h"
#include "unicode/unistr.h"
#include "unicode/uchar.h"
#include "uassert.h"
#include "uelement.h"
#include "ustr_imp.h"

U_NAMESPACE_BEGIN

//========================================
// Read-only implementation
//========================================

int8_t
UnicodeString::doCaseCompare(int32_t start,
                             int32_t length,
                             const UChar *srcChars,
                             int32_t srcStart,
                             int32_t srcLength,
                             uint32_t options) const
{
  // compare illegal string values
  // treat const UChar *srcChars==NULL as an empty string
  if(isBogus()) {
    return -1;
  }

  // pin indices to legal values
  pinIndices(start, length);

  if(srcChars == NULL) {
    srcStart = srcLength = 0;
  }

  // get the correct pointer
  const UChar *chars = getArrayStart();

  chars += start;
  if(srcStart!=0) {
    srcChars += srcStart;
  }

  if(chars != srcChars) {
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t result=u_strcmpFold(chars, length, srcChars, srcLength,
                                options|U_COMPARE_IGNORE_CASE, &errorCode);
    if(result!=0) {
      return (int8_t)(result >> 24 | 1);
    }
  } else {
    // get the srcLength if necessary
    if(srcLength < 0) {
      srcLength = u_strlen(srcChars + srcStart);
    }
    if(length != srcLength) {
      return (int8_t)((length - srcLength) >> 24 | 1);
    }
  }
  return 0;
}

//========================================
// Write implementation
//========================================

UnicodeString &
UnicodeString::caseMap(const UCaseMap *csm,
                       UStringCaseMapper *stringCaseMapper) {
  if(isEmpty() || !isWritable()) {
    // nothing to do
    return *this;
  }

  UChar oldBuffer[2 * US_STACKBUF_SIZE];
  UChar *oldArray;
  int32_t oldLength = length();
  int32_t newLength;
  UBool writable = isBufferWritable();
  UErrorCode errorCode = U_ZERO_ERROR;

  // Try to avoid heap-allocating a new character array for this string.
  if (writable ? oldLength <= UPRV_LENGTHOF(oldBuffer) : oldLength < US_STACKBUF_SIZE) {
    // Short string: Copy the contents into a temporary buffer and
    // case-map back into the current array, or into the stack buffer.
    UChar *buffer = getArrayStart();
    int32_t capacity;
    oldArray = oldBuffer;
    u_memcpy(oldBuffer, buffer, oldLength);
    if (writable) {
      capacity = getCapacity();
    } else {
      // Switch from the read-only alias or shared heap buffer to the stack buffer.
      if (!cloneArrayIfNeeded(US_STACKBUF_SIZE, US_STACKBUF_SIZE, /* doCopyArray= */ FALSE)) {
        return *this;
      }
      U_ASSERT(fUnion.fFields.fLengthAndFlags & kUsingStackBuffer);
      buffer = fUnion.fStackFields.fBuffer;
      capacity = US_STACKBUF_SIZE;
    }
    newLength = stringCaseMapper(csm, buffer, capacity, oldArray, oldLength, NULL, &errorCode);
    if (U_SUCCESS(errorCode)) {
      setLength(newLength);
      return *this;
    } else if (errorCode == U_BUFFER_OVERFLOW_ERROR) {
      // common overflow handling below
    } else {
      setToBogus();
      return *this;
    }
  } else {
    // Longer string or read-only buffer:
    // Collect only changes and then apply them to this string.
    // Case mapping often changes only small parts of a string,
    // and often does not change its length.
    oldArray = getArrayStart();
    Edits edits;
    edits.setWriteUnchanged(FALSE);
    UChar replacementChars[200];
    int32_t replacementLength = stringCaseMapper(
            csm, replacementChars, UPRV_LENGTHOF(replacementChars),
            oldArray, oldLength, &edits, &errorCode);
    UErrorCode editsError = U_ZERO_ERROR;
    if (edits.setErrorCode(editsError)) {
      setToBogus();
      return *this;
    }
    newLength = oldLength + edits.lengthDelta();
    if (U_SUCCESS(errorCode)) {
      if (!cloneArrayIfNeeded(newLength, newLength)) {
        return *this;
      }
      int32_t index = 0;  // index into this string
      int32_t replIndex = 0;  // index into replacementChars
      for (Edits::Iterator iter = edits.getCoarseIterator(); iter.next(errorCode);) {
        if (iter.changed) {
          doReplace(index, iter.oldLength, replacementChars, replIndex, iter.newLength);
          replIndex += iter.newLength;
        }
        index += iter.newLength;
      }
      if (U_FAILURE(errorCode)) {
        setToBogus();
      }
      U_ASSERT(replIndex == replacementLength);
      return *this;
    } else if (errorCode == U_BUFFER_OVERFLOW_ERROR) {
      // common overflow handling below
    } else {
      setToBogus();
      return *this;
    }
  }

  // Handle buffer overflow, newLength is known.
  // We need to allocate a new buffer for the internal string case mapping function.
  // This is very similar to how doReplace() keeps the old array pointer
  // and deletes the old array itself after it is done.
  // In addition, we are forcing cloneArrayIfNeeded() to always allocate a new array.
  int32_t *bufferToDelete = 0;
  if (!cloneArrayIfNeeded(newLength, newLength, FALSE, &bufferToDelete, TRUE)) {
    return *this;
  }
  errorCode = U_ZERO_ERROR;
  newLength = stringCaseMapper(csm, getArrayStart(), getCapacity(),
                               oldArray, oldLength, NULL, &errorCode);
  if (bufferToDelete) {
    uprv_free(bufferToDelete);
  }
  if (U_SUCCESS(errorCode)) {
    setLength(newLength);
  } else {
    setToBogus();
  }
  return *this;
}

UnicodeString &
UnicodeString::foldCase(uint32_t options) {
  UCaseMap csm=UCASEMAP_INITIALIZER;
  csm.csp=ucase_getSingleton();
  csm.options=options;
  return caseMap(&csm, ustrcase_internalFold);
}

U_NAMESPACE_END

// Defined here to reduce dependencies on break iterator
U_CAPI int32_t U_EXPORT2
uhash_hashCaselessUnicodeString(const UElement key) {
    U_NAMESPACE_USE
    const UnicodeString *str = (const UnicodeString*) key.pointer;
    if (str == NULL) {
        return 0;
    }
    // Inefficient; a better way would be to have a hash function in
    // UnicodeString that does case folding on the fly.
    UnicodeString copy(*str);
    return copy.foldCase().hashCode();
}

// Defined here to reduce dependencies on break iterator
U_CAPI UBool U_EXPORT2
uhash_compareCaselessUnicodeString(const UElement key1, const UElement key2) {
    U_NAMESPACE_USE
    const UnicodeString *str1 = (const UnicodeString*) key1.pointer;
    const UnicodeString *str2 = (const UnicodeString*) key2.pointer;
    if (str1 == str2) {
        return TRUE;
    }
    if (str1 == NULL || str2 == NULL) {
        return FALSE;
    }
    return str1->caseCompare(*str2, U_FOLD_CASE_DEFAULT) == 0;
}
