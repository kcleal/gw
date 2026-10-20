/** Local-only, read-only File adapter. Never uploads or copies the complete file. */
export function mountLocalFile(FS, Module, directory, name, file) {
    if(!Module.Asyncify?.handleAsync) throw new Error('This build needs Asyncify for local byte-range reads');
    FS.mkdirTree(directory);
    FS.createDataFile(directory,name,new Uint8Array(0),true,false,false);
    const path=directory+'/'+name, node=FS.lookupPath(path,{follow:true}).node;
    node.gwLocalFile=file;
    const getattr=node.node_ops.getattr;
    node.node_ops={...node.node_ops,getattr(n){return {...getattr(n),size:file.size};}};
    node.stream_ops={...node.stream_ops,
        read(stream,buffer,offset,length,position){
            if(!length || position>=file.size)return 0;
            return Module.Asyncify.handleAsync(async()=>{
                const data=new Uint8Array(await file.slice(position,Math.min(position+length,file.size)).arrayBuffer());
                buffer.set(data,offset);return data.length;
            });
        },
        llseek(stream,offset,whence){
            const position=offset+(whence===1?stream.position:whence===2?file.size:0);
            if(position<0)throw new FS.ErrnoError(28);return position;
        }
    };
    return path;
}
export function createLocalLoader({FS,Module,command,status}) {
    const pending=new Map(), modes=new Map();let serial=0,busy=false;
    return {async accept(files,options={track:true,tiles:false}){
        if(busy){status('A file is opening; please wait');return;}
        for(const file of files){pending.set(file.name,file);modes.set(file.name,{...options});}
        busy=true;
        try {
            // References first when a selection includes both reference and reads.
            const selected=[...pending.values()].filter(f=>! /\.(bai|csi|fai|gzi|crai|tbi)$/i.test(f.name));
            selected.sort((a,b)=>Number(/\.(fa|fasta)$/i.test(b.name))-Number(/\.(fa|fasta)$/i.test(a.name)));
            for(const file of selected){
                let indexes=[];
                if(/\.bam$/i.test(file.name)){
                    const stem=file.name.slice(0,-4);
                    const index=[file.name+'.bai',stem+'.bai',file.name+'.csi',stem+'.csi'].map(n=>pending.get(n)).find(Boolean);
                    if(!index){status('Waiting for index — select '+file.name+'.bai or '+file.name+'.csi');continue;}
                    indexes=[index];
                }else if(/\.(fa|fasta)$/i.test(file.name)){
                    const index=pending.get(file.name+'.fai');
                    if(!index){status('Waiting for index — select '+file.name+'.fai');continue;}
                    indexes=[index];
                }else if(/\.(gz|bgz|cram|bcf)$/i.test(file.name)){
                    throw new Error('Use an uncompressed FASTA, indexed BAM, or an uncompressed annotation/VCF in this local loader');
                }
                const directory='/local/'+(++serial), safe=file.name.replace(/[^a-zA-Z0-9_.-]/g,'_');
                // Normalize alternate reads.bai to reads.bam.bai for HTSlib discovery.
                for(const index of indexes){
                    const suffix=index.name.endsWith('.csi')?'.csi':index.name.endsWith('.bai')?'.bai':'.fai';
                    mountLocalFile(FS,Module,directory,safe+suffix,index);
                }
                const path=mountLocalFile(FS,Module,directory,safe,file);
                status('Opening '+file.name+' locally…');
                const mode=modes.get(file.name)||options;
                if(/\.(fa|fasta)$/i.test(file.name))await command('load genome '+path);
                else if(/\.(vcf|bed)$/i.test(file.name)){
                    if(mode.tiles)await command('load tiled '+path);
                    if(mode.track || !mode.tiles)await command('load track '+path);
                }else await command('load '+path);
                modes.delete(file.name);
                pending.delete(file.name);for(const index of indexes)pending.delete(index.name);
                status('');
            }
        }catch(error){status(error.message);}
        finally{busy=false;}
    }};
}
