#include "downloads.h"
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <sstream>
#include <fstream>
#include <zlib.h>
#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif
namespace Online {
std::string downloadDirectory(const std::string& iniPath) {
    return (std::filesystem::path(iniPath).parent_path()/"gw_downloads").string();
}
#ifndef __EMSCRIPTEN__
namespace {
void transfer(const std::string& url, const std::filesystem::path& path, const std::shared_ptr<DownloadProgress>& progress) {
    if(!httpURL(url))throw std::runtime_error("Downloads require an HTTP(S) URL");
    if(progress->cancelled)throw std::runtime_error("Download cancelled");
    auto temporary=path.string()+".part";
    auto* file=std::fopen(temporary.c_str(),"wb");
    if(!file)throw std::runtime_error("Cannot write to the GW downloads folder");
    auto* curl=curl_easy_init();
    if(!curl){std::fclose(file);std::filesystem::remove(temporary);throw std::runtime_error("Cannot initialize download");}
    progress->received=0;progress->total=0;
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,60L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,file);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,+[](void* ptr,curl_off_t total,curl_off_t received,curl_off_t,curl_off_t)->int{
        auto* p=static_cast<DownloadProgress*>(ptr);p->total=total;p->received=received;return p->cancelled?1:0;
    });
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,progress.get());
    const auto result=curl_easy_perform(curl);
    long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(curl);const int closed=std::fclose(file);
    if(result!=CURLE_OK || closed || progress->cancelled) {
        std::filesystem::remove(temporary);
        if(progress->cancelled)throw std::runtime_error("Download cancelled");
        throw std::runtime_error("Download failed (HTTP "+std::to_string(status)+"): "+curl_easy_strerror(result));
    }
    try{std::filesystem::rename(temporary,path);}
    catch(...){std::filesystem::remove(temporary);throw;}
}
}
Entry download(Entry entry,const std::string& directory,const std::shared_ptr<DownloadProgress>& progress) {
    const auto original=entry;
    auto name=resourceName(entry.url);
    if(name.empty()||name=="."||name=="..")throw std::runtime_error("The URL needs a filename");
    for(auto& c:name)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='.'&&c!='-'&&c!='_')c='_';
    if(original.format=="ucsc-raw")name=original.assembly+"."+original.providerId+".tsv.gz";
    if(original.format=="ucsc-table")name=original.assembly+"."+original.providerId+".bed";
    for(auto& c:name)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='.'&&c!='-'&&c!='_')c='_';
    const auto folder=std::filesystem::path(directory);std::filesystem::create_directories(folder);
    auto data=folder/name;
    const auto dot=name.find('.');const auto stem=name.substr(0,dot),extension=dot==std::string::npos?std::string():name.substr(dot);
    for(unsigned n=2;std::filesystem::exists(data)||std::filesystem::exists(data.string()+".part");++n)data=folder/(stem+"_"+std::to_string(n)+extension);
    entry.url=data.string();entry.indexURL.clear();entry.compressedIndexURL.clear();
    auto suffix=[&](const char* s){return name.size()>=std::char_traits<char>::length(s)&&name.compare(name.size()-std::char_traits<char>::length(s),std::char_traits<char>::length(s),s)==0;};
    const bool indexed=original.format!="ucsc-table"&&original.format!="ucsc-raw"&&(original.category=="reference"||original.category=="alignment"||suffix(".gz")||suffix(".bgz")||suffix(".bcf"));
    if(indexed) {
        std::string last;
        for(const auto& candidate:indexCandidates(original))try {
            auto ext=resourceName(candidate);ext=ext.substr(ext.find_last_of('.')+1);
            const auto target=data.string()+"."+ext;
            transfer(candidate,target,progress);entry.indexURL=target;break;
        }catch(const std::runtime_error& error){if(progress->cancelled)throw;last=error.what();}
        if(entry.indexURL.empty())throw std::runtime_error("Data downloaded, but its required index failed: "+last);
    }
    if(original.category=="reference"&&(suffix(".gz")||suffix(".bgz"))) {
        auto url=original.compressedIndexURL;
        if(url.empty()){auto q=original.url.find('?');url=original.url.substr(0,q)+".gzi"+(q==std::string::npos?"":original.url.substr(q));}
        entry.compressedIndexURL=data.string()+".gzi";transfer(url,entry.compressedIndexURL,progress);
    }
    if(original.format=="ucsc-table"){
        const auto compressed=data.string()+".source.gz";
        try{
            transfer(original.url,compressed,progress);
            auto* input=gzopen(compressed.c_str(),"rb");
            if(!input)throw std::runtime_error("Cannot read downloaded UCSC table");
            std::ofstream output(data.string()+".part");
            try{
                std::string line;char buffer[16384];
                while(gzgets(input,buffer,sizeof(buffer))){
                    if(progress->cancelled)throw std::runtime_error("Download cancelled");
                    line+=buffer;
                    if(line.size()>16*1024*1024)throw std::runtime_error("UCSC table row is too large");
                    if(!line.empty()&&line.back()=='\n'){output<<tableRowToBed(line,original.tableColumns);line.clear();}
                    if(!output)throw std::runtime_error("Cannot save converted UCSC track");
                }
                int code=Z_OK;gzerror(input,&code);if(code!=Z_OK&&code!=Z_STREAM_END)throw std::runtime_error("Invalid compressed UCSC table");
                if(!line.empty())output<<tableRowToBed(line,original.tableColumns);
                output.close();if(!output)throw std::runtime_error("Cannot save converted UCSC track");
            }catch(...){gzclose(input);throw;}
            gzclose(input);std::filesystem::rename(data.string()+".part",data);std::filesystem::remove(compressed);
        }catch(...){std::filesystem::remove(compressed);std::filesystem::remove(data.string()+".part");throw;}
        entry.format="bed";entry.tableColumns.clear();
    }else transfer(original.url,data,progress);
    return entry;
}
#endif
}
