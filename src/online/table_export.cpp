#include "online_catalogue.h"
#include <map>
#include <sstream>
#include <stdexcept>
#include <algorithm>
namespace Online {
namespace {
std::vector<std::string> split(const std::string& text,char delimiter){
    std::vector<std::string> fields;std::istringstream input(text);std::string field;
    while(std::getline(input,field,delimiter))fields.push_back(field);
    if(delimiter=='\t'&&!text.empty()&&text.back()=='\t')fields.emplace_back();
    return fields;
}
long long number(const std::string& value){
    size_t used=0;long long result;
    try{result=std::stoll(value,&used);}catch(...){throw std::runtime_error("Invalid coordinate in UCSC table");}
    if(used!=value.size()||result<0)throw std::runtime_error("Invalid coordinate in UCSC table");
    return result;
}
}
std::string tableRowToBed(const std::string& row,const std::string& columns){
    if(row.empty()||row[0]=='#')return {};
    auto text=row;while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'))text.pop_back();
    auto values=split(text,'\t'),names=split(columns,',');
    if(values.size()!=names.size())throw std::runtime_error("UCSC table does not match its column schema");
    std::map<std::string,std::string> fields;
    for(size_t i=0;i<names.size();++i)fields[names[i]]=values[i];
    auto get=[&](const char* key,const std::string& fallback=""){auto found=fields.find(key);return found==fields.end()?fallback:found->second;};
    const auto chrom=get("chrom"),startText=get("chromStart",get("txStart")),endText=get("chromEnd",get("txEnd"));
    const auto start=number(startText),end=number(endText);
    if(chrom.empty()||end<start)throw std::runtime_error("Invalid interval in UCSC table");
    auto strand=get("strand",".");if(strand!="+"&&strand!="-")strand=".";
    std::string score="0";try{score=std::to_string(std::min(1000LL,number(get("score","0"))));}catch(const std::runtime_error&){}
    std::string out=chrom+'\t'+startText+'\t'+endText+'\t'+get("name",get("name2","."))+'\t'+score+'\t'+strand;
    auto countText=get("exonCount",get("blockCount"));
    if(!countText.empty()){
        const auto count=number(countText);
        if(count>1000000)throw std::runtime_error("Too many blocks in UCSC table row");
        auto starts=split(get("exonStarts",get("chromStarts",get("blockStarts"))),','),ends=split(get("exonEnds"),','),sizes=split(get("blockSizes"),',');
        if(starts.size()!=static_cast<size_t>(count)||(ends.empty()?sizes.size():ends.size())!=static_cast<size_t>(count))throw std::runtime_error("Invalid exon blocks in UCSC table");
        std::string relative,blockSizes;
        for(size_t i=0;i<starts.size();++i){
            const auto pos=number(starts[i]);const auto offset=ends.empty()?pos:pos-start;
            const auto size=ends.empty()?number(sizes[i]):number(ends[i])-pos;
            if(offset<0||size<0||offset>end-start||size>end-start-offset)throw std::runtime_error("Exon outside UCSC transcript");
            relative+=std::to_string(offset)+',';blockSizes+=std::to_string(size)+',';
        }
        auto thickStart=number(get("thickStart",get("cdsStart",startText))),thickEnd=number(get("thickEnd",get("cdsEnd",endText)));
        if(thickStart<start||thickEnd<thickStart||thickEnd>end)throw std::runtime_error("Invalid coding interval in UCSC table");
        out+='\t'+std::to_string(thickStart)+'\t'+std::to_string(thickEnd)+"\t0\t"+countText+'\t'+blockSizes+'\t'+relative;
    }
    return out+'\n';
}
}
