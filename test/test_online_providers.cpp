#include "online/online_catalogue.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
static std::string read(const char* path){std::ifstream file(path);std::ostringstream out;out<<file.rdbuf();return out.str();}
int main(int argc,char** argv){
    auto entries=Online::parseUcscSearch(R"({"positionMatches":[{"name":"trackDb","matches":[{"posName":"genes:Genes:Genes: extra detail"},{"posName":"signal:Signal:Signal track"}]},{"name":"genes","matches":[{"posName":"ignore"}]}]})","hg38");
    assert(entries.size()==2&&entries[0].providerId=="genes"&&entries[0].description=="Genes: extra detail");
    auto entry=Online::resolveUcscTrack(R"({"type":"bed","type":"bigGenePred","bigDataUrl":"/gbdb/hg38/genes.bgp","longLabel":"Gene <b>models</b>","dataTime":"2026-01"})",entries[0]);
    assert(entry.url=="https://hgdownload.soe.ucsc.edu/gbdb/hg38/genes.bgp"&&entry.format=="bigbed"&&entry.description=="Gene models"&&Online::regionalFile(entry));
    entry.sizeBytes=123456;auto copy=Online::parseCatalogue(Online::encodeEntry(entry)).at(0);
    assert(copy.provider==entry.provider&&copy.providerId==entry.providerId&&copy.sizeBytes==123456);
    assert(Online::resolveUcscTrack(R"({"type":"genePred"})",entries[0]).format=="unavailable");
    auto table=Online::resolveUcscTrack(R"({"type":"bed 4 +","columnTypes":[{"name":"bin"},{"name":"chrom"},{"name":"chromStart"},{"name":"chromEnd"},{"name":"name"}]})",entries[0]);
    assert(table.format=="ucsc-table"&&table.tableColumns=="bin,chrom,chromStart,chromEnd,name");
    assert(table.url.find("/database/")!=std::string::npos);
    auto raw=Online::resolveUcscTrack(R"({"columnTypes":[{"name":"name"},{"name":"value"}]})",entries[0]);assert(raw.format=="ucsc-raw");
    try{Online::ucscSearchURL("hg38;bad", "genes");assert(false);}catch(const std::runtime_error&){}
    assert(Online::ucscSearchURL("hg38","RNA seq").find("RNA%20seq")!=std::string::npos);
    try{Online::parseCatalogue(R"({"version":1,"version":1,"entries":[]})");assert(false);}catch(const std::runtime_error&){}
    assert(Online::fileSizeLabel(-1)=="Size unknown"&&Online::fileSizeLabel(0)=="0 B"&&Online::fileSizeLabel(11494021)=="11.0 MiB");
    Online::Entry bed;bed.category="track";bed.url="https://data.test/regions.bed";assert(!Online::regionalFile(bed));bed.url+=".gz";assert(Online::regionalFile(bed));
    if(argc==3){
        auto live=Online::parseUcscSearch(read(argv[1]),"hg38");assert(!live.empty());
        bool found=false;for(const auto& item:live)if(item.providerId=="ncbiRefSeqOther"){
            auto resolved=Online::resolveUcscTrack(read(argv[2]),item);assert(resolved.format=="bigbed");found=true;
            std::cout<<"Live UCSC search: "<<live.size()<<" tracks; resolved "<<resolved.url<<"\n";
        }assert(found);
    }
    std::cout<<"Online provider discovery, URL resolution, metadata and size tests passed\n";
}
