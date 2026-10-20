#include "online_files.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include <htslib/sam.h>
#include <htslib/vcf.h>
#include <htslib/tbx.h>
#include "bigWig.h"
#include <stdexcept>
#include <algorithm>
#include <sstream>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif
namespace Online {
namespace {
std::map<std::string, Entry> companions;
std::string canonical(std::string name){if(name.rfind("chr",0)==0)name.erase(0,3);if(name=="MT")name="M";return name;}
bool ends(const std::string& s,const std::string& suffix){return s.size()>=suffix.size() && s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0;}
void compatible(const Contigs& expected,const Contigs& actual){
    if(expected.empty()||actual.empty())return;
    bool shared=false;
    for(const auto& c:actual){auto it=expected.find(c.first);if(it==expected.end())continue;shared=true;if(c.second>0&&it->second>0&&c.second!=it->second)throw std::runtime_error("Reference assembly mismatch at "+c.first+" (contig lengths differ). Choose a matching reference/data set or close incompatible files.");}
    if(!shared)throw std::runtime_error("No chromosomes in common with the loaded data/reference");
}
Contigs faiContigs(faidx_t* fai){Contigs out;for(int i=0;i<faidx_nseq(fai);++i){auto name=faidx_iseq(fai,i);out[canonical(name)]=faidx_seq_len(fai,name);}return out;}
// Keep the view inside the newly loaded reference. Local alignments with different
// chromosome spelling cannot supply reads for this view and are closed explicitly.
void reconcileReferenceView(Manager::GwPlot* p) {
    if(!p->fai || faidx_nseq(p->fai)==0)return;
    p->collections.clear();
    if(p->regions.empty())p->regions.emplace_back(faidx_iseq(p->fai,0),0,1000);
    for(auto& region:p->regions){
        std::string chrom=region.chrom;
        if(!faidx_has_seq(p->fai,chrom.c_str())){
            chrom.clear();
            for(int i=0;i<faidx_nseq(p->fai);++i){
                const char* candidate=faidx_iseq(p->fai,i);
                if(canonical(candidate)==canonical(region.chrom)){chrom=candidate;break;}
            }
            if(chrom.empty())chrom=faidx_iseq(p->fai,0);
        }
        const int length=faidx_seq_len(p->fai,chrom.c_str());
        const int span=std::min(std::max(1,region.end-region.start),std::max(1,length));
        const int start=std::clamp(region.start,0,std::max(0,length-span));
        Utils::Region updated(chrom,start,start+span);
        updated.chromLen=length;
        if(chrom==region.chrom)updated.markers=region.markers;
        region=std::move(updated);
    }
    p->regionSelection=std::clamp(p->regionSelection,0,int(p->regions.size())-1);
    for(int i=int(p->headers.size())-1;i>=0;--i){
        const auto& path=p->bam_paths[i];
        if(httpURL(path)||companions.count(path))continue; // Keep separately opened online sources.
        bool missing=false;
        for(const auto& region:p->regions)
            if(sam_hdr_name2tid(p->headers[i],region.chrom.c_str())<0){missing=true;break;}
        if(!missing)continue;
        auto& out=p->terminalOutput?std::cout:p->outStr;
        out<<"Closed local alignment (chromosome name differs from the reference view): "<<path<<"\n";
        p->tracks.erase(std::remove_if(p->tracks.begin(),p->tracks.end(),[i](const auto& track){
            return track.kind==HGW::FType::INTRON&&track.bamIndex==i;
        }),p->tracks.end());
        for(auto& track:p->tracks)if(track.kind==HGW::FType::INTRON&&track.bamIndex>i)--track.bamIndex;
        p->removeBam(i);
    }
    p->tracksLayoutDirty=true;
}

}
void rememberIndexes(const Entry& e){companions[e.url]=e;}
std::string formatFor(const std::string& path){auto it=companions.find(path);return it==companions.end()?"":it->second.format;}
std::string referenceAssembly(const Manager::GwPlot* p){
    auto it=companions.find(p->reference);std::string assembly;
    if(it!=companions.end())assembly=it->second.assembly;
    else{
        const auto genomes=p->opts.myIni.get("genomes");
        if(genomes.has(p->opts.genome_tag)&&genomes.get(p->opts.genome_tag)==p->reference)assembly=p->opts.genome_tag;
    }
    if(assembly=="GRCh38")return "hg38";if(assembly=="GRCh37")return "hg19";
    return assembly;
}
const char* indexPath(const std::string& path){auto it=companions.find(path);return it==companions.end()||it->second.indexURL.empty()?nullptr:it->second.indexURL.c_str();}
faidx_t* loadFai(const std::string& path){auto it=companions.find(path);if(it==companions.end())return fai_load(path.c_str());const auto& e=it->second;return fai_load3(path.c_str(),e.indexURL.empty()?nullptr:e.indexURL.c_str(),e.compressedIndexURL.empty()?nullptr:e.compressedIndexURL.c_str(),0);}
Contigs contigsFor(const Manager::GwPlot* p,bool replacing){
    if(!replacing&&p->fai)return faiContigs(p->fai);
    Contigs out;for(auto* h:p->headers)for(int i=0;i<h->n_targets;++i)out[canonical(h->target_name[i])]=h->target_len[i];return out;
}
Entry validate(Entry e,const Contigs& expected,bool check){
    auto name=resourceName(e.url);
    if(name.empty()||name.find_first_of("\r\n\t ")!=std::string::npos)throw std::runtime_error("A file URL with a supported extension is required");
    if(check&&!expected.empty()){
        auto chr1=expected.find("1");long long length=e.assembly=="hg38"||e.assembly=="GRCh38"?248956422:e.assembly=="hg19"||e.assembly=="GRCh37"?249250621:e.assembly=="hs1"?248387328:0;
        if(length&&chr1!=expected.end()&&chr1->second!=length && e.category!="reference")throw std::runtime_error("Catalogue assembly differs from the loaded reference. Open its matching reference first.");
    }
    Contigs actual;
    if(ends(name,".vcf")||ends(name,".vcf.gz")||ends(name,".bcf")){
        auto* file=bcf_open(e.url.c_str(),"r");auto* header=file?bcf_hdr_read(file):nullptr;
        const bool readable=header!=nullptr;
        if(header){
            int n=0;const char** names=bcf_hdr_seqnames(header,&n);
            for(int i=0;i<n;++i){
                long long length=0;auto* record=bcf_hdr_get_hrec(header,BCF_HL_CTG,"ID",names[i],nullptr);
                int key=record?bcf_hrec_find_key(record,"length"):-1;
                if(key>=0)length=std::strtoll(record->vals[key],nullptr,10);
                actual[canonical(names[i])]=length;
            }
            free(names);bcf_hdr_destroy(header);
        }
        if(file)bcf_close(file);
        if(!readable)throw std::runtime_error("Cannot read VCF/BCF header");
    }
    const auto candidates=indexCandidates(e);bool good=false;
    if(e.category=="reference"){
        if(ends(name,".gz")||ends(name,".bgz"))if(e.compressedIndexURL.empty()){auto q=e.url.find('?');e.compressedIndexURL=e.url.substr(0,q)+".gzi"+(q==std::string::npos?"":e.url.substr(q));}
        for(const auto& idx:candidates){auto* f=fai_load3(e.url.c_str(),idx.c_str(),e.compressedIndexURL.empty()?nullptr:e.compressedIndexURL.c_str(),0);if(!f)continue;actual=faiContigs(f);fai_destroy(f);e.indexURL=idx;good=!actual.empty();break;}
    }else if(e.category=="alignment"){
        auto* f=sam_open(e.url.c_str(),"r");if(!f)throw std::runtime_error("Cannot read alignment header");auto* h=sam_hdr_read(f);
        if(h){for(int i=0;i<h->n_targets;++i)actual[canonical(h->target_name[i])]=h->target_len[i];for(const auto& idx:candidates){auto* index=sam_index_load2(f,e.url.c_str(),idx.c_str());if(index){hts_idx_destroy(index);e.indexURL=idx;good=true;break;}}sam_hdr_destroy(h);}sam_close(f);
    }else if(e.format=="bigbed"||e.format=="bigwig"||ends(name,".bb")||ends(name,".bigbed")||ends(name,".bw")||ends(name,".bigwig")){
        if(bwInit(1<<17)!=0)throw std::runtime_error("Cannot initialize indexed track reader");
        auto* f=(e.format=="bigbed"||ends(name,".bb")||ends(name,".bigbed"))?bbOpen(e.url.c_str(),nullptr):bwOpen(e.url.c_str(),nullptr,"r");
        if(f){if(f->cl)for(int64_t i=0;i<f->cl->nKeys;++i)actual[canonical(f->cl->chrom[i])]=f->cl->len[i];bwClose(f);good=true;}
    }else if(ends(name,".bcf")){
        auto* f=bcf_open(e.url.c_str(),"r");auto* h=f?bcf_hdr_read(f):nullptr;if(h){for(const auto& idx:candidates){auto* index=bcf_index_load2(e.url.c_str(),idx.c_str());if(index){hts_idx_destroy(index);e.indexURL=idx;good=true;break;}}bcf_hdr_destroy(h);}if(f)bcf_close(f);
    }else if(ends(name,".gz")||ends(name,".bgz")){
        for(const auto& idx:candidates){auto* index=tbx_index_load2(e.url.c_str(),idx.c_str());if(!index)continue;int n=0;auto** names=tbx_seqnames(index,&n);for(int i=0;i<n;++i)actual.emplace(canonical(names[i]),0);free(names);tbx_destroy(index);e.indexURL=idx;good=true;break;}
    }else if(ends(name,".bed")||ends(name,".vcf")||ends(name,".gff")||ends(name,".gff3")||ends(name,".gtf")){
        auto* f=hts_open(e.url.c_str(),"r");if(f){hts_close(f);good=true;}
    }else throw std::runtime_error("Supported online formats: FASTA, BAM/CRAM, indexed VCF/BCF/BED, bigBed/bigWig, and small text annotations");
    if(!good)throw std::runtime_error("Cannot open file or required index. Check the URL, index URL and access permissions.");
    // A reference switch must succeed even if existing files use another assembly.
    if(check && e.category!="reference")compatible(expected,actual);
    return e;
}
std::vector<Entry> validateGroup(std::vector<Entry> entries,const Contigs& reference,const Contigs& alignments,bool check){
    auto expected=reference;
    for(auto& e:entries){e=validate(e,e.category=="reference"?alignments:expected,check);
        if(e.category=="reference"){auto* f=fai_load3(e.url.c_str(),e.indexURL.c_str(),e.compressedIndexURL.empty()?nullptr:e.compressedIndexURL.c_str(),0);if(!f)throw std::runtime_error("Cannot read bundle reference index");expected=faiContigs(f);fai_destroy(f);}}
    return entries;
}
void openGroup(Manager::GwPlot* p,const std::vector<Entry>& entries,bool asTrack,bool check,bool validated,bool asTable){
    const auto prepared=validated?entries:validateGroup(entries,contigsFor(p,false),contigsFor(p,true),check);
#ifdef __EMSCRIPTEN__
    EM_ASM({Module.gwOnlineCommitting=true;});
#endif
    for(const auto& e:prepared){
        if(e.category!="variant")open(p,e,true,false,true);
        else{
            if(asTrack)open(p,e,true,false,true);
            if(asTable||!asTrack)open(p,e,false,false,true);
        }
    }
    if(!prepared.empty()&&!prepared.front().locus.empty()){auto locus=prepared.front().locus;std::ostringstream output;Commands::run_command_map(p,locus,output);}
}
void open(Manager::GwPlot* p,const Entry& entry,bool asTrack,bool check,bool validated){
    auto e=validated?entry:validate(entry,contigsFor(p,entry.category=="reference"),check);
    rememberIndexes(e);
    if(e.category=="reference"){
        std::ostringstream output;p->loadGenome(e.url,output);
        if(p->reference!=e.url)throw std::runtime_error(output.str().empty()?"Could not load reference":output.str());
        reconcileReferenceView(p);
    }else{auto path=e.url;if(!p->addTrack(path,true,asTrack,asTrack))throw std::runtime_error("GW could not open this file");}
    std::string refresh="refresh"; std::ostringstream output; Commands::run_command_map(p,refresh,output);
}
#ifndef __EMSCRIPTEN__
std::int64_t fetchFileSize(const std::string& url){
    if(!httpURL(url))return -1;
    auto* c=curl_easy_init();if(!c)return -1;
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_NOBODY,1L);
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
#if LIBCURL_VERSION_NUM >= 0x075500 // libcurl 7.85.0
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,10L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_off_t size=-1;auto result=curl_easy_perform(c);
    if(result==CURLE_OK)curl_easy_getinfo(c,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&size);
    curl_easy_cleanup(c);return size<0?-1:std::int64_t(size);
}
std::string fetchCatalogue(const std::string& url,const std::shared_ptr<std::atomic<bool>>& cancelled){
    if(!httpURL(url))throw std::runtime_error("Catalogue URL must use HTTP(S)");
    auto* c=curl_easy_init();if(!c)throw std::runtime_error("Cannot initialize HTTP client");
    struct Buffer{std::string text;std::shared_ptr<std::atomic<bool>> stop;} buffer{{},cancelled};
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
#if LIBCURL_VERSION_NUM >= 0x075500 // libcurl 7.85.0
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_TIMEOUT,30L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,+[](char* data,size_t size,size_t count,void* ptr)->size_t{auto& b=*static_cast<Buffer*>(ptr);size_t n=size*count;if(b.stop->load()||n>2*1024*1024-b.text.size())return 0;b.text.append(data,n);return n;});curl_easy_setopt(c,CURLOPT_WRITEDATA,&buffer);
    auto result=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);curl_easy_cleanup(c);if(cancelled->load())throw std::runtime_error("Cancelled");if(result!=CURLE_OK)throw std::runtime_error(std::string("Online metadata request failed (HTTP ")+std::to_string(status)+"): "+curl_easy_strerror(result));return buffer.text;
}
#endif
}
