#ifndef ossimCurlHeaderCache_HEADER
#define ossimCurlHeaderCache_HEADER
#include <ossim/base/ossimTimer.h>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <mutex>  // For std::unique_lock
#include <thread>

namespace ossim
{
   class CurlHeaderCacheNode
   {
   public:
      CurlHeaderCacheNode(ossim_int64 filesize)
      :m_timestamp(ossimTimer::instance()->tick()),
      m_filesize(filesize)
      {

      }

      ossimTimer::Timer_t m_timestamp;
      ossim_int64         m_filesize;
   };

   class CurlHeaderCache
   {
   public:
      typedef std::shared_ptr<CurlHeaderCacheNode> Node_t;
      typedef std::string Key_t;
      typedef ossim_int64 TimeIndex_t;
      typedef std::map<Key_t, Node_t > CacheType;
      typedef std::map<TimeIndex_t, Key_t > CacheTimeIndexType;

      CurlHeaderCache();
      virtual ~CurlHeaderCache();
      bool getCachedFilesize(const Key_t& key, ossim_int64& filesize)const;
      void addHeader(const Key_t& key, Node_t& node);
      void setMaxCacheEntries(ossim_int64 maxEntries);
      void touchEntry(const Key_t& key);

      static std::shared_ptr<CurlHeaderCache> instance();

   protected:
      void shrinkEntries();
      void touchEntryProtected(const Key_t& key);

      static std::shared_ptr<CurlHeaderCache> m_instance;
      mutable std::mutex m_mutex;

      CacheType m_cache;
      CacheTimeIndexType m_cacheTime;
      ossim_int64 m_maxCacheEntries;

   };
}

#endif
