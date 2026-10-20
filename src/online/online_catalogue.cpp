#include "online_catalogue.h"
#include <map>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <iomanip>
namespace Online {
namespace {
struct Json {
    char type = 0;
    std::string text;
    std::map<std::string, Json> object;
    std::vector<Json> array;
    std::string get(const std::string& key) const {
        auto it=object.find(key); return it==object.end()?"":it->second.text;
    }
};
// Bounded catalogue reader: reject ambiguous duplicate keys and malformed input.
class Reader {
    const std::string& s; size_t p=0; bool duplicates=false;
    [[noreturn]] void bad() const {throw std::runtime_error("Invalid online catalogue JSON");}
    char peek(){while(p<s.size() && std::isspace((unsigned char)s[p]))++p;return p<s.size()?s[p]:0;}
    void expect(char c){if(peek()!=c)bad();++p;}
    unsigned hex(){unsigned n=0;for(int i=0;i<4;++i){if(p==s.size())bad();char c=s[p++];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)bad();n=n*16+d;}return n;}
    void utf8(std::string& out,unsigned c){if(c<128)out+=char(c);else if(c<2048){out+=char(192|(c>>6));out+=char(128|(c&63));}else if(c<65536){out+=char(224|(c>>12));out+=char(128|((c>>6)&63));out+=char(128|(c&63));}else{out+=char(240|(c>>18));out+=char(128|((c>>12)&63));out+=char(128|((c>>6)&63));out+=char(128|(c&63));}}
    std::string string(){expect('"');std::string out;while(p<s.size()){unsigned char c=s[p++];if(c=='"')return out;if(c<32)bad();if(c!='\\'){out+=char(c);continue;}if(p==s.size())bad();switch(s[p++]){case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'u':{unsigned c=hex();if(c>=0xd800&&c<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')bad();unsigned low=hex();if(low<0xdc00||low>0xdfff)bad();c=0x10000+((c-0xd800)<<10)+low-0xdc00;}else if(c>=0xdc00&&c<=0xdfff)bad();utf8(out,c);break;}default:bad();}}bad();}
    Json value(int depth){if(depth>24)bad();Json v;v.type=peek();if(v.type=='{'){++p;if(peek()!='}')for(;;){auto key=string();expect(':');auto child=value(depth+1);if(v.object.count(key)&&!duplicates)bad();v.object.insert_or_assign(key,std::move(child));if(peek()=='}')break;expect(',');}expect('}');}else if(v.type=='['){++p;if(peek()!=']')for(;;){v.array.push_back(value(depth+1));if(peek()==']')break;expect(',');}expect(']');}else if(v.type=='"')v.text=string();else{size_t a=p;while(p<s.size()&&s[p]!=','&&s[p]!=']'&&s[p]!='}'&&!std::isspace((unsigned char)s[p]))++p;auto token=s.substr(a,p-a);if(token!="true"&&token!="false"&&token!="null"){char* end=nullptr;std::strtod(token.c_str(),&end);if(token.empty()||end!=token.c_str()+token.size())bad();}v.text=token;}return v;}
public: explicit Reader(const std::string& text,bool allowDuplicates=false):s(text),duplicates(allowDuplicates){if(s.size()>2*1024*1024)throw std::runtime_error("Catalogue exceeds 2 MiB");}
    Json read(){auto v=value(0);if(peek())bad();return v;}
};
std::string quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){const char* h="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=char(c);}return out+'"';}
}
bool httpURL(const std::string& s){return (s.rfind("https://",0)==0||s.rfind("http://",0)==0)&&s.find_first_of("\r\n\t ") == std::string::npos;}
std::string resourceName(const std::string& url){auto s=url.substr(0,url.find_first_of("?#"));return s.substr(s.find_last_of('/')+1);}
std::vector<Entry> parseCatalogue(const std::string& text, bool allowMountedPaths){
    auto root=Reader(text).read();std::vector<Entry> out;
    if(root.type=='['){
        // IGV/Broad adapter; the startup picker uses this same decoder.
        for(const auto& j:root.array){Entry e;e.id="igv:"+j.get("id");e.name=j.get("name");e.category="reference";e.assembly=j.get("id");e.format="fasta";e.url=j.get("fastaURL");e.indexURL=j.get("indexURL");e.compressedIndexURL=j.get("compressedIndexURL");e.source="IGV / Broad";
            if((e.assembly=="hg19"||e.assembly=="hg38")&&e.url.find("igv-genepattern-org.s3.amazonaws.com")!=std::string::npos){
                e.url=e.assembly=="hg19"?"https://igv.org/genomes/data/hg19/hg19.fasta":"https://igv.org/genomes/data/hg38/hg38.fa";e.indexURL=e.url+".fai";
            }
            if(httpURL(e.url))out.push_back(e);}
    }else{
        if(root.get("version")!="1"||!root.object.count("entries")||root.object.at("entries").type!='[')throw std::runtime_error("Expected catalogue version 1 and entries");
        for(const auto& j:root.object.at("entries").array){Entry e;
#define FIELD(f) e.f=j.get(#f)
            FIELD(id);FIELD(name);FIELD(category);FIELD(assembly);FIELD(sample);FIELD(technology);FIELD(format);FIELD(url);FIELD(indexURL);FIELD(compressedIndexURL);FIELD(source);FIELD(release);FIELD(attribution);FIELD(regionsURL);FIELD(locus);FIELD(provider);FIELD(providerId);FIELD(description);FIELD(tableColumns);
#undef FIELD
            if(j.object.count("sizeBytes")){
                const auto size=j.get("sizeBytes");size_t used=0;
                try{e.sizeBytes=std::stoll(size,&used);}catch(...){throw std::runtime_error("Invalid file size");}
                if(used!=size.size()||e.sizeBytes< -1)throw std::runtime_error("Invalid file size");
            }
            if(j.object.count("bundle")){
                const auto& members=j.object.at("bundle");if(members.type!='['||members.array.size()>16)throw std::runtime_error("Bundle must contain at most 16 resource IDs");
                for(const auto& member:members.array){if(member.type!='"')throw std::runtime_error("Bundle members must be resource IDs");e.bundle.push_back(member.text);}
            }
            if(e.id.empty()||e.name.empty()||!(httpURL(e.url)||(allowMountedPaths&&(e.url.rfind("/online/",0)==0||e.url.rfind("/persistent/gw_downloads/",0)==0||e.url.rfind("/tmp/gw_downloads/",0)==0))))throw std::runtime_error("Entry needs id, name and HTTP(S) URL");
            if(e.category!="reference"&&e.category!="alignment"&&e.category!="track"&&e.category!="variant")throw std::runtime_error("Unknown catalogue category");
            for(auto* url:{&e.indexURL,&e.compressedIndexURL,&e.regionsURL})if(!url->empty()&&!(httpURL(*url)||(allowMountedPaths&&(url->rfind("/online/",0)==0||url->rfind("/persistent/gw_downloads/",0)==0||url->rfind("/tmp/gw_downloads/",0)==0))))throw std::runtime_error("Companion URLs must be HTTP(S)");
            out.push_back(e);
        }
    }
    std::map<std::string,bool> ids;for(const auto& e:out)if(!ids.emplace(e.id,true).second)throw std::runtime_error("Duplicate catalogue ID: "+e.id);
    return out;
}
std::string encodeEntry(const Entry& e){std::string out="{\"version\":1,\"entries\":[{";bool first=true;
#define FIELD(f) if(!first)out+=',';first=false;out+=quote(#f)+":"+quote(e.f)
    FIELD(id);FIELD(name);FIELD(category);FIELD(assembly);FIELD(sample);FIELD(technology);FIELD(format);FIELD(url);FIELD(indexURL);FIELD(compressedIndexURL);FIELD(source);FIELD(release);FIELD(attribution);FIELD(regionsURL);FIELD(locus);FIELD(provider);FIELD(providerId);FIELD(description);FIELD(tableColumns);
#undef FIELD
    out+=",\"sizeBytes\":"+std::to_string(e.sizeBytes);
    out+=",\"bundle\":[";first=true;for(const auto& id:e.bundle){if(!first)out+=',';first=false;out+=quote(id);}out+=']';
    return out+"}]}";
}
std::string encodeCatalogue(const std::vector<Entry>& entries){
    std::string out="{\"version\":1,\"entries\":[";bool first=true;
    for(const auto& e:entries){auto text=encodeEntry(e);auto a=text.find("[{"),b=text.rfind("}]");if(!first)out+=',';first=false;out+=text.substr(a+1,b-a);}
    return out+"]}";
}
std::vector<Entry> resolveBundle(const Entry& entry,const std::vector<Entry>& catalogue){
    if(entry.bundle.empty())return {entry};
    std::vector<Entry> result;
    std::map<std::string,bool> seen;
    for(const auto& id:entry.bundle){if(!seen.emplace(id,true).second)throw std::runtime_error("Duplicate bundle member");auto it=std::find_if(catalogue.begin(),catalogue.end(),[&](const auto& e){return e.id==id;});if(it==catalogue.end())throw std::runtime_error("Bundle resource missing: "+id);auto e=*it;e.bundle.clear();result.push_back(e);}
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.category=="reference"&&b.category!="reference";});return result;
}
std::vector<std::string> indexCandidates(const Entry& e){if(!e.indexURL.empty())return {e.indexURL};auto url=e.url;auto q=url.find('?');auto path=url.substr(0,q);auto query=q==std::string::npos?"":url.substr(q);auto end=[&](const char* ext){return path.size()>=std::string(ext).size()&&path.compare(path.size()-std::string(ext).size(),std::string(ext).size(),ext)==0;};
    if(e.category=="reference")return {path+".fai"+query};
    if(end(".bam"))return {path+".bai"+query,path.substr(0,path.size()-4)+".bai"+query,path+".csi"+query};
    if(end(".cram"))return {path+".crai"+query,path.substr(0,path.size()-5)+".crai"+query};
    if(end(".bcf"))return {path+".csi"+query};
    if(end(".gz")||end(".bgz"))return {path+".tbi"+query,path+".csi"+query};
    return {};
}
namespace {
std::string encodeURL(const std::string& text){
    const char* hex="0123456789ABCDEF";std::string out;
    for(unsigned char c:text){if(std::isalnum(c)||c=='_'||c=='-'||c=='.')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
std::string plainText(const std::string& html){
    std::string out;bool tag=false;
    for(char c:html){if(c=='<')tag=true;else if(c=='>')tag=false;else if(!tag)out+=c;if(out.size()>=1000)break;}
    return out;
}
}
bool regionalFile(const Entry& e){
    if(e.category=="reference"||e.category=="alignment"||e.format=="bigbed"||e.format=="bigwig")return true;
    auto name=resourceName(e.url);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
    for(const char* suffix:{".bb",".bigbed",".bw",".bigwig",".bcf",".vcf.gz",".bed.gz",".gff.gz",".gff3.gz",".gtf.gz",".bgz"}){
        std::string ext=suffix;if(name.size()>=ext.size()&&name.compare(name.size()-ext.size(),ext.size(),ext)==0)return true;
    }
    return false;
}
std::string fileSizeLabel(std::int64_t size){
    if(size<0)return "Size unknown";
    const char* units[]={"B","KiB","MiB","GiB","TiB"};double number=double(size);int unit=0;
    while(number>=1024&&unit<4){number/=1024;++unit;}
    std::ostringstream out;out<<std::fixed<<std::setprecision(unit?1:0)<<number<<' '<<units[unit];return out.str();
}
std::string ucscSearchURL(const std::string& assembly,const std::string& query){
    if(assembly.empty()||assembly.size()>80||assembly.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
        throw std::runtime_error("Enter a UCSC assembly ID, such as hg38, hg19 or mm39");
    if(query.size()<2||query.size()>128)throw std::runtime_error("Enter 2–128 characters to search UCSC tracks");
    return "https://api.genome.ucsc.edu/search?genome="+encodeURL(assembly)+";search="+encodeURL(query)+";categories=trackDb";
}
std::vector<Entry> parseUcscSearch(const std::string& text,const std::string& assembly){
    const auto root=Reader(text,true).read();std::vector<Entry> out;
    if(root.object.count("error"))throw std::runtime_error(root.get("error"));
    if(!root.get("genome").empty()&&root.get("genome")!=assembly)throw std::runtime_error("UCSC returned a different assembly");
    auto groups=root.object.find("positionMatches");if(groups==root.object.end())throw std::runtime_error("UCSC returned no track-search result");
    for(const auto& group:groups->second.array){
        if(group.get("name")!="trackDb")continue;
        auto matches=group.object.find("matches");if(matches==group.object.end())continue;
        for(const auto& match:matches->second.array){
            auto text=match.get("posName");if(text.empty())text=match.get("position");
            auto colon=text.find(':');if(colon==std::string::npos)continue;
            auto track=text.substr(0,colon);if(track.empty())continue;
            Entry e;e.provider="ucsc";e.providerId=track;e.id="ucsc:"+assembly+":"+track;
            auto second=text.find(':',colon+1);e.name=plainText(text.substr(colon+1,second==std::string::npos?second:second-colon-1));
            if(e.name.empty())e.name=track;
            e.description=plainText(second==std::string::npos?match.get("description"):text.substr(second+1));
            e.assembly=assembly;e.category="track";e.source="UCSC";
            e.url="https://api.genome.ucsc.edu/list/schema?genome="+encodeURL(assembly)+";track="+encodeURL(track);
            out.push_back(e);if(out.size()>=300)return out;
        }
    }
    return out;
}
Entry resolveUcscTrack(const std::string& text,Entry e){
    const auto root=Reader(text,true).read();if(root.object.count("error"))throw std::runtime_error(root.get("error"));
    if((!root.get("genome").empty()&&root.get("genome")!=e.assembly)||(!root.get("track").empty()&&root.get("track")!=e.providerId))throw std::runtime_error("UCSC returned metadata for another track or assembly");
    auto type=root.get("type");type=type.substr(0,type.find(' '));auto url=root.get("bigDataUrl");
    if(url.empty()){
        auto columns=root.object.find("columnTypes");
        if(columns!=root.object.end())for(const auto& column:columns->second.array){if(!e.tableColumns.empty())e.tableColumns+=',';e.tableColumns+=column.get("name");}
        auto has=[&](const std::string& column){return (","+e.tableColumns+",").find(","+column+",")!=std::string::npos;};
        if(has("chrom")&&((has("chromStart")&&has("chromEnd"))||(has("txStart")&&has("txEnd")))){
            e.format="ucsc-table";
            e.url="https://hgdownload.soe.ucsc.edu/goldenPath/"+encodeURL(e.assembly)+"/database/"+encodeURL(e.providerId)+".txt.gz";
            e.description+=" — Download as a local BED annotation track";
        }else if(!e.tableColumns.empty()){
            e.format="ucsc-raw";
            e.url="https://hgdownload.soe.ucsc.edu/goldenPath/"+encodeURL(e.assembly)+"/database/"+encodeURL(e.providerId)+".txt.gz";
            e.description+=" — Download table dump; this schema cannot be drawn as a genomic track.";
        }else{e.format="unavailable";e.description+=" — Track group; select an individual data track.";}
        return e;
    }
    if(type=="bigWig")e.format="bigwig";
    else if(type=="bigBed"||type=="bigGenePred"||type=="bigNarrowPeak"||type=="bigBroadPeak")e.format="bigbed";
    else if(type=="vcfTabix"||type=="vcfPhasedTrio")e.format="vcf.gz";
    else if(type.rfind("big",0)==0)e.format="bigbed";
    else {e.format="unavailable";e.description+=" — This file format cannot currently be opened by GW.";return e;}
    if(url.rfind("/gbdb/",0)==0)url="https://hgdownload.soe.ucsc.edu"+url;
    else if(url.rfind("//",0)==0)url="https:"+url;
    if(!httpURL(url))throw std::runtime_error("UCSC did not return an HTTP(S) data URL");
    e.url=url;e.release=root.get("dataTime");auto description=root.get("longLabel");if(!description.empty())e.description=plainText(description);
    return e;
}
std::vector<Entry> bundledCatalogue(){return parseCatalogue(
#include "../../resources/online/catalogue.inc"
);}
}
