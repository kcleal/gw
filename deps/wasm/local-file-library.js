// Emscripten JS library: suspend once per complete WASI vectored read.
// Suspending separately in FS.stream_ops.read is unsafe for readv's multiple
// buffers: on rewind the JS loop restarts and can consume the wrong result.
mergeInto(LibraryManager.library, {
  $gwLocalRead__deps: ['$Asyncify'],
  $gwLocalRead: function(stream, iov, count, result, offset) {
    return Asyncify.handleAsync(async function() {
      try {
        var vectors=[];
        for(var i=0;i<count;i++) vectors.push([HEAPU32[(iov+i*8)>>2],HEAPU32[(iov+i*8+4)>>2]]);
        var position=offset===undefined?stream.position:offset;
        var total=0;
        var file=stream.node.gwLocalFile;
        for(var vector of vectors) {
          var length=Math.min(vector[1],Math.max(0,file.size-position));
          if(!length) {if(vector[1])break;continue;}
          var data=new Uint8Array(await file.slice(position,position+length).arrayBuffer());
          HEAPU8.set(data,vector[0]);total+=data.length;position+=data.length;
          if(data.length<vector[1])break;
        }
        if(offset===undefined)stream.position=position;
        HEAPU32[result>>2]=total;
        return 0;
      } catch(error) {return error.errno || 29;}
    });
  },
  fd_read__deps: ['$SYSCALLS', '$doReadv', '$gwLocalRead'],
  fd_read__async: true,
  fd_read: function(fd, iov, count, result) {
    try {
      var stream=SYSCALLS.getStreamFromFD(fd);
      if(stream.node.gwLocalFile)return gwLocalRead(stream,iov,count,result);
      HEAPU32[result>>2]=doReadv(stream,iov,count);return 0;
    } catch(error) {return error.errno || 29;}
  },
  fd_pread__deps: ['$SYSCALLS', '$doReadv', '$gwLocalRead'],
  fd_pread__async: true,
  fd_pread__i53abi: true,
  fd_pread: function(fd, iov, count, offset, result) {
    try {
      if(isNaN(offset))return 61;
      var stream=SYSCALLS.getStreamFromFD(fd);
      if(stream.node.gwLocalFile)return gwLocalRead(stream,iov,count,result,offset);
      HEAPU32[result>>2]=doReadv(stream,iov,count,offset);return 0;
    } catch(error) {return error.errno || 29;}
  }
});
