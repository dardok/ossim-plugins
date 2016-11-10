//---
//
// License: MIT
//
// Author: Garrett Potts
// 
// Description:
// 
// OSSIM Amazon Web Services (AWS) S3 streambuf definition.
//
//---
// $Id$

#include "ossimS3StreamBuffer.h"

#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/GetObjectResult.h>
#include <aws/s3/model/HeadObjectRequest.h>

#include <aws/core/Aws.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h> 
#include <iostream>
#include <ossim/base/ossimUrl.h>
//static const char* KEY = "test-file.txt";
//static const char* BUCKET = "ossimlabs";
#include <cstdio> /* for EOF */
#include <streambuf>
#include <sstream>
#include <iosfwd>
#include <ios>
#include <vector>
#include <cstring> /* for memcpy */

using namespace Aws::S3;
using namespace Aws::S3::Model;

ossim::S3StreamBuffer::S3StreamBuffer(ossim_int64 blockSize)
   :
   m_bucket(""),
   m_key(""),
   m_buffer(blockSize),
   m_bufferActualDataSize(0),
   m_currentBlockPosition(-1),
   m_bufferPtr(0),
   m_fileSize(0),
   m_opened(false)
   //m_mode(0)
{
//    std::cout << "CONSTRUCTED!!!!!" << std::endl;
//  setp(0);
   setg(m_bufferPtr, m_bufferPtr, m_bufferPtr);
}

ossim_int64 ossim::S3StreamBuffer::getBlockIndex(ossim_int64 byteOffset)const
{
   ossim_int64 blockNumber = -1;
  
   if(byteOffset < (ossim_int64)m_fileSize)
   {
      if(m_buffer.size()>0)
      {
         blockNumber = byteOffset/m_buffer.size();
      }    
   }

   return blockNumber;
}

ossim_int64 ossim::S3StreamBuffer::getBlockOffset(ossim_int64 byteOffset)const
{
   ossim_int64 blockOffset = -1;
  
   if(m_buffer.size()>0)
   {
      blockOffset = byteOffset%m_buffer.size();
   }    

   return blockOffset;
}

bool ossim::S3StreamBuffer::getBlockRangeInBytes(ossim_int64 blockIndex, 
                                                 ossim_int64& startRange, 
                                                 ossim_int64& endRange)const
{
   bool result = false;

   if(blockIndex >= 0)
   {
      startRange = blockIndex*m_buffer.size();
      endRange = startRange + m_buffer.size()-1;

      result = true;    
   }

   return result;
}

bool ossim::S3StreamBuffer::loadBlock(ossim_int64 absolutePosition)
{
   bool result = false;
   m_bufferPtr = 0;
   GetObjectRequest getObjectRequest;
   std::stringstream stringStream;
   ossim_int64 startRange, endRange;
   ossim_int64 blockIndex = getBlockIndex(absolutePosition);
   if((absolutePosition < 0) || (absolutePosition > m_fileSize)) return false;
   //std::cout << "CURRENT BYTE LOCATION = " << absoluteLocation << std::endl;
   if(getBlockRangeInBytes(blockIndex, startRange, endRange))
   {
      stringStream << "bytes=" << startRange << "-" << endRange;
      getObjectRequest.WithBucket(m_bucket.c_str())
         .WithKey(m_key.c_str()).WithRange(stringStream.str().c_str());
      auto getObjectOutcome = m_client.GetObject(getObjectRequest);

      if(getObjectOutcome.IsSuccess())
      {
//        std::cout << "GOOD CALL!!!!!!!!!!!!\n";
         Aws::IOStream& bodyStream = getObjectOutcome.GetResult().GetBody();
         ossim_uint64 bufSize = getObjectOutcome.GetResult().GetContentLength();
//        std::cout << "SIZE OF RESULT ======== " << bufSize << std::endl;
         m_bufferActualDataSize = bufSize;
         bodyStream.read(&m_buffer.front(), bufSize);
         m_bufferPtr = &m_buffer.front();

         ossim_int64 delta = absolutePosition-startRange;
         setg(m_bufferPtr, m_bufferPtr + delta, m_bufferPtr+m_bufferActualDataSize);
         m_currentBlockPosition = startRange;
         result = true;
         //std::cout << "Successfully retrieved object from s3 with value: " << std::endl;
         //std::cout << getObjectOutcome.GetResult().GetBody().rdbuf() << std::endl << std::endl;;  
      }
      else
      {
         m_bufferActualDataSize = 0;
      }
   }

   return result;
}

ossim::S3StreamBuffer* ossim::S3StreamBuffer::open (const char* connectionString,  
                                                    std::ios_base::openmode m)
{
   std::string temp(connectionString);
   return open(temp, m);
}

ossim::S3StreamBuffer* ossim::S3StreamBuffer::open (const std::string& connectionString, 
                                                    std::ios_base::openmode /* mode */)
{
   // bool result = false;
   ossimUrl url(connectionString);
   clearAll();
   // m_mode = mode;

   if(url.getProtocol() == "s3")
   {
      m_bucket = url.getIp().c_str();
      m_key = url.getPath().c_str();

      if(!m_bucket.empty() && !m_key.empty())
      {
         HeadObjectRequest headObjectRequest;
         headObjectRequest.WithBucket(m_bucket.c_str())
            .WithKey(m_key.c_str());
         auto headObject = m_client.HeadObject(headObjectRequest);
         if(headObject.IsSuccess())
         {
            m_fileSize = headObject.GetResult().GetContentLength();
            m_opened = true;
            m_currentBlockPosition = 0;
         }
      }
   }

   if(m_opened) return this;

   return 0;
}



void ossim::S3StreamBuffer::clearAll()
{
   m_bucket = "";
   m_key    = "";
   m_fileSize = 0;
   m_opened = false;
   m_currentBlockPosition = 0;
}


int ossim::S3StreamBuffer::underflow()
{
   if(!is_open())
   {
      return EOF;
   }
   else if( !withinWindow() )
   {
      ossim_int64 absolutePosition = getAbsoluteByteOffset();
      if(absolutePosition < 0)
      {
         return EOF;
      }

      if(!loadBlock(absolutePosition))
      {
         return EOF;
      }
   }

//  std::cout << "GPTR CHARACTER ========== " << (int)(*gptr()) << std::endl;
   return (int)(*gptr());
}

ossim::S3StreamBuffer::pos_type ossim::S3StreamBuffer::seekoff(off_type offset, 
                                                               std::ios_base::seekdir dir,
                                                               std::ios_base::openmode mode)
{ 
  // std::cout << "ossim::S3StreamBuffer::seekoff\n";
   pos_type result = pos_type(off_type(-1));
   // bool withinBlock = true;
   if((mode & std::ios_base::in)&&
      (mode & std::ios_base::out))
   {
      return result;
   }
   switch(dir)
   {
      case std::ios_base::beg:
      {
         ossim_int64 absolutePosition = getAbsoluteByteOffset();
         // really would like to figure out a better way but for now
         // we have to have one valid block read in to properly adjust
         // gptr() with gbump
         //
         if(!gptr())
         {
            if(!loadBlock(offset))
            {
               return result;
            }
         }
         if((offset <= m_fileSize)&&
            (offset >=0))
         {
            result = pos_type(offset);
         }
         if(!gptr())
         {
            if(!loadBlock(result))
            {
               return EOF;
            }
         }
         else if(mode  & std::ios_base::in)
         {
            absolutePosition = getAbsoluteByteOffset();
            gbump(offset - absolutePosition);
         }
         break;
      }
      case std::ios_base::cur:
      {
         if(!gptr())
         {
            if(!loadBlock(0))
            {
               return result;
            }
         }
         result = getAbsoluteByteOffset();
         if(!offset)
         {
            result = getAbsoluteByteOffset();
         }
         else
         {
            result += offset;
         }
         gbump(offset);
         break;
      }
      case std::ios_base::end:
      {
         ossim_int64 absolutePosition  = m_fileSize + offset;
         if(!gptr())
         {
            // if(absolutePosition==m_fileSize)
            // {
            //    if(!loadBlock(absolutePosition))
            //    {
            //       return result;
            //    }
            // }
            // else
            // {
               if(!loadBlock(absolutePosition))
               {
                  return result;
               }               
//            }
         }
         ossim_int64 currentAbsolutePosition = getAbsoluteByteOffset();
         ossim_int64 delta = absolutePosition-currentAbsolutePosition;

//         std::cout << "CURRENT ABSOLUTE POSITION === " << currentAbsolutePosition << std::endl;
//         std::cout << "CURRENT ABSOLUTE delta POSITION === " << delta << std::endl;
         if(mode & std::ios_base::in )
         {
            gbump(delta);
            result = absolutePosition;
         }
         break;
      }
      default:
      {
         break;
      }
   }

   return result; 
} 

ossim::S3StreamBuffer::pos_type ossim::S3StreamBuffer::seekpos(pos_type pos, std::ios_base::openmode mode)
{
  // std::cout << "ossim::S3StreamBuffer::seekpos: " << pos << std::endl;
   pos_type result = pos_type(off_type(-1));
   // Currently we must initialize to a block
   if(!gptr())
   {
      if(!loadBlock(pos))
      {
         return result;
      }
   }

   ossim_int64 absoluteLocation = getAbsoluteByteOffset();
   if(mode & std::ios_base::in)
   {
      if((pos >= 0)&&(pos < m_fileSize))
      {
         ossim_int64 delta = pos-absoluteLocation;
         // std::cout << "DELTA ============= " << delta << std::endl;
         if(delta)
         {
            gbump(delta);
         }
         result = pos;
      }
   }

   return result;
}

std::streamsize ossim::S3StreamBuffer::xsgetn(char_type* s, std::streamsize n)
{      
//  std::cout << "ossim::S3StreamBuffer::xsgetn" << std::endl;

   if(!is_open()) return EOF;
   // unsigned long int bytesLeftToRead = egptr()-gptr();
   // initialize if we need to to load the block at current position
   if((!withinWindow())&&is_open())
   {
      if(!gptr())
      {
         if(!loadBlock(0))
         {
            return EOF;
         }
      }
      else if(!loadBlock(getAbsoluteByteOffset()))
      {
         return EOF;
      }
   }
   ossim_int64 bytesNeedToRead = n;
   // ossim_int64 bytesToRead = 0;
   ossim_int64 bytesRead = 0;
   ossim_int64 currentAbsolutePosition = getAbsoluteByteOffset();
   // ossim_int64 startOffset, endOffset;
   if(currentAbsolutePosition >= (ossim_int64)m_fileSize)
   {
      return EOF;
   }
   else if((currentAbsolutePosition + bytesNeedToRead)>(ossim_int64)m_fileSize)
   {
      bytesNeedToRead = (m_fileSize - currentAbsolutePosition);
   }

   while(bytesNeedToRead > 0)
   {
      currentAbsolutePosition = getAbsoluteByteOffset();
      if(!withinWindow())
      {
         if(!loadBlock(currentAbsolutePosition))
         {
            return bytesRead;
         }
         currentAbsolutePosition = getAbsoluteByteOffset();
      }

      // get each bloc  
      if(currentAbsolutePosition>=0)
      {
         //getBlockRangeInBytes(getBlockIndex(m_currentBlockPosition), startOffset, endOffset);      
    
         ossim_int64 delta = (egptr()-gptr());//(endOffset - m_currentBlockPosition)+1;

         if(delta <= bytesNeedToRead)
         {
            std::memcpy(s+bytesRead, gptr(), delta);
            //m_currentBlockPosition += delta;
            //setg(eback(), egptr(), egptr());
            bytesRead+=delta;
            bytesNeedToRead-=delta;
            gbump(delta);
         }
         else
         {
            std::memcpy(s+bytesRead, gptr(), bytesNeedToRead);
            gbump(bytesNeedToRead);
            bytesRead+=bytesNeedToRead;
            bytesNeedToRead=0;
         }
      }
      else
      {
         break;
      }
   }
   return std::streamsize(bytesRead);
}

ossim_int64 ossim::S3StreamBuffer::getAbsoluteByteOffset()const
{
   ossim_int64 result = -1;

   if(m_currentBlockPosition >= 0)
   {
      result = m_currentBlockPosition;
      if(gptr()&&eback())
      {
         result += (gptr()-eback());
      }
   }

   return result;
}

bool ossim::S3StreamBuffer::withinWindow()const
{
   if(!gptr()) return false;
   return ((gptr()>=eback()) && (gptr()<egptr()));
}

