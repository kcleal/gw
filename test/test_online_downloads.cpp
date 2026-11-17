#include "online/downloads.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
int main(int argc,char** argv){
    assert(argc==3);const std::string base=argv[1],folder=argv[2];
    auto progress=std::make_shared<Online::DownloadProgress>();
    Online::Entry reference;reference.id="ref";reference.name="Test";reference.category="reference";reference.url=base+"/ref.fa";
    const auto local=Online::download(reference,folder,progress);
    assert(std::filesystem::exists(local.url)&&std::filesystem::exists(local.indexURL));
    std::ifstream input(local.url);std::string line;std::getline(input,line);assert(line==">chr1");
    assert(std::filesystem::path(local.url).parent_path()==folder);
    auto again=Online::download(reference,folder,progress);assert(std::filesystem::path(again.url).filename()=="ref_2.fa");
    assert(local.url.rfind(folder,0)==0&&progress->received>0);
    auto variant=reference;variant.category="variant";variant.url=base+"/calls.vcf.gz";
    const auto vcf=Online::download(variant,folder,progress);
    assert(vcf.indexURL.substr(vcf.indexURL.size()-4)==".csi");
    Online::Entry table;table.category="track";table.format="ucsc-table";table.assembly="hg19";table.providerId="cpgIslandExt";
    table.url=base+"/table.txt.gz";table.tableColumns="bin,chrom,chromStart,chromEnd,name";
    auto bed=Online::download(table,folder,progress);
    assert(std::filesystem::path(bed.url).filename()=="hg19.cpgIslandExt.bed"&&bed.format=="bed");
    std::ifstream converted(bed.url);std::getline(converted,line);assert(line=="chr1\t10\t20\tCpG\t0\t.");
    assert(Online::tableRowToBed("tx\tchr1\t-\t10\t50\t15\t45\t2\t10,40,\t20,50,", "name,chrom,strand,txStart,txEnd,cdsStart,cdsEnd,exonCount,exonStarts,exonEnds")=="chr1\t10\t50\ttx\t0\t-\t15\t45\t0\t2\t10,10,\t0,30,\n");
    table.format="ucsc-raw";table.providerId="summary";
    auto raw=Online::download(table,folder,progress);
    assert(raw.format=="ucsc-raw"&&std::filesystem::path(raw.url).filename()=="hg19.summary.tsv.gz"&&raw.indexURL.empty());
    auto failed=reference;failed.url=base+"/missing.fa";
    bool rejected=false;try{Online::download(failed,folder,progress);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    progress->cancelled=true;rejected=false;
    try{Online::download(reference,folder,progress);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    progress->cancelled=false;progress->received=0;
    auto slow=reference;slow.category="track";slow.url=base+"/slow.bed";
    std::atomic<bool> finished{false};
    std::thread cancel([&]{while(!finished && progress->received<16384)std::this_thread::sleep_for(std::chrono::milliseconds(1));progress->cancelled=true;});
    rejected=false;try{Online::download(slow,folder,progress);}catch(const std::runtime_error&){rejected=true;}
    finished=true;cancel.join();assert(rejected);
    for(const auto& file:std::filesystem::recursive_directory_iterator(folder))assert(file.path().extension()!=".part");
    assert(Online::downloadDirectory("/tmp/settings/.gw.ini")=="/tmp/settings/gw_downloads");
    std::cout<<"PASS: downloads, companions, index fallback, cancellation, failure cleanup and folder layout\n";
}
