#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>
#include <fstream>
#include<iostream>
#include"vdiheader.h"
#include"vdifile.h"
#include"vdifunctions.h"
#include"partitionData.h"
#include"mbr.h"
#include"partitionFunctions.h"
#include"superblock.h"
#include"superblockFunctions.h"
#include"blockGroupDescriptor.h"
#include"ext2File.h"
#include"inodeFunctions.h"
#include<string>
#include<vector>
#include<algorithm>
#include<cmath>
using namespace std;
/*
This funtion is used to fetch the inode contained in the inode table
*/
int fetchInode(struct ext2File *f,struct vdifile *vdi,struct blockGroupDescriptor bg[], uint32_t iNum,
               struct inode& in,int offsetToSuperBlock,int translationMapData[],unsigned char inodeMetaData[]){
    int readInodeBytes,
        blockSize,
        blockGroupNumber,
        offsetToGivenInode;
    blockSize=1024<<f->superblock.s_log_block_size;
    blockGroupNumber= (iNum-1)/f->superblock.s_inodes_per_group;
    if(blockSize==1024){
    offsetToGivenInode=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset+
                      (bg[blockGroupNumber].bg_inode_table-1-blockGroupNumber*f->superblock.s_blocks_per_group)*
                      (blockSize)+((iNum-1)%f->superblock.s_inodes_per_group)*f->superblock.s_inode_size;
    }
    if(blockSize==4096){
      offsetToGivenInode=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset-1024+
                        (bg[blockGroupNumber].bg_inode_table-blockGroupNumber*f->superblock.s_blocks_per_group)*
                        (blockSize)+((iNum-1)%f->superblock.s_inodes_per_group)*f->superblock.s_inode_size;
    }
    int physicalAddress= actualPage(offsetToGivenInode,vdi,translationMapData);
    vdiSeek(vdi,physicalAddress,SEEK_SET);
    readInodeBytes= vdiRead(vdi,&in,sizeof(in));
    vdiSeek(vdi,physicalAddress,SEEK_SET);
    vdiRead(vdi,inodeMetaData,128);
    return offsetToGivenInode;
}
/*
checks whether or not inode is in used
*/
bool inodeInUse(struct ext2File *f,unsigned char inodeBitMap[], int & indexToFreeInode){
    uint8_t oneByte;
    bool allocated= true;
    for(int i=0;i<(1024<<f->superblock.s_log_block_size);i++){
    oneByte= inodeBitMap[i];
    for(int j=0;j<8;j++){
      allocated= (oneByte>>j & 0x1);
      if(!allocated){
        indexToFreeInode= i*8 +(7-j);
        return (allocated);
      }
    }
  }
  return allocated;
}
/*
fetch the inode bitmap representing the particular block Group
*/
unsigned char * fetchInodeBitMap(struct ext2File *f,struct vdifile * vdi, struct blockGroupDescriptor bg[], uint32_t iNum,
                                int offsetToSuperBlock,int translationMapData[]){
  int readBitBytes,
      blockSize,
      blockGroupNumber,
      offsetInodeBitMap;
  blockSize=1024<<f->superblock.s_log_block_size;
  blockGroupNumber= (iNum-1)/f->superblock.s_inodes_per_group;
  if(blockSize==1024){
    offsetInodeBitMap=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset+
                    (bg[blockGroupNumber].bg_inode_bitmap-1-blockGroupNumber*f->superblock.s_blocks_per_group)*(blockSize);
  }
  if(blockSize==4096){
    offsetInodeBitMap=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset-1024+
                    (bg[blockGroupNumber].bg_inode_bitmap-blockGroupNumber*f->superblock.s_blocks_per_group)*(blockSize);
  }
  unsigned char inodeBitMap[blockSize];
  int physicalAddress= actualPage(offsetInodeBitMap,vdi,translationMapData);
  vdiSeek(vdi,physicalAddress,SEEK_SET);
  readBitBytes= vdiRead(vdi,inodeBitMap,sizeof(inodeBitMap));
  return inodeBitMap;
}
/*
allocate the unused inode and mark the byte as used in inode bitmap
*/
void allocateInode(int & indexToFreeInode,unsigned char inodeBitMap[]){
    int byteOffset= indexToFreeInode/8;
    int offsetIntoByte= indexToFreeInode%8;
    int byteData= inodeBitMap[byteOffset];
    inodeBitMap[byteOffset]= 0x1<<(7-offsetIntoByte) | byteData;
}
/*
it marks the particular inode as freeInode
*/
void freeInode(int iNum,unsigned char inodeBitMap[]){
    int byteOffset= (iNum-1)/8;
    int offsetIntoByte= 7-(iNum-1)%8;
    uint8_t byteData= inodeBitMap[byteOffset];
    inodeBitMap[byteOffset]= (byteData>>offsetIntoByte ^ 0x1);
}
/*
it fetches the given block number
*/
 void fetchBlock(struct ext2File * ext2,int blockNumber,struct vdifile*file,struct mbrSector mbr,int translationMapData[],int k,int buffer[]){
  int offsetToGivenBlock= mbr.partitionEntryInfo[0].logicalBlocking*512+blockNumber*(1024<<ext2->superblock.s_log_block_size);
  int physicalAddress= actualPage(offsetToGivenBlock,file,translationMapData);
  vdiSeek(file,physicalAddress,SEEK_SET);
  vdiRead(file,buffer,4*k);
}
/*
it helps to fetch the given block from file
*/
int fetchBlockFromFile(struct inode * i, int bNum,struct superBlock sBlock,
                       struct ext2File * ext2,struct vdifile*file,struct mbrSector mbr,int translationMapData[]){
  vector<int>blockList;
  int k= (1024<<sBlock.s_log_block_size)/4;
  int buffer[k],
      index,
      offsetInto;
  if(bNum<12){
     int totalSize=(sizeof(i->i_block)/sizeof((i->i_block)[0]));
     for(int x: i->i_block)
     blockList.push_back(x);
     fetchBlock(ext2,blockList[bNum],file,mbr,translationMapData,k,buffer);
     return blockList[bNum];
  }
  else if (bNum<12+k){
      if(i->i_block[12]==0){
        return 0;
      }
      fetchBlock(ext2,i->i_block[12], file, mbr, translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bNum= bNum-12;
      index= bNum%k;
      if(blockList[index]!=0){
        fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
        return blockList[index];
      }
  }
  else if(bNum<12+k+pow(k,2)){
      if(i->i_block[13]==0){
        return 0;
      }
      fetchBlock(ext2,i->i_block[13],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bNum= bNum-12-k;
      index= bNum/k;
      offsetInto= bNum%k;
      if(blockList[index] !=0){
          fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
          blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      }
      //Finally it can be used to locate direct block of the file
      if(blockList[offsetInto]!=0){
        fetchBlock(ext2,blockList[offsetInto],file,mbr,translationMapData,k,buffer);
        return blockList[offsetInto];
      }
  }
  else{
      if(i->i_block[14]==0){
        return 0;
      }
      fetchBlock(ext2,i->i_block[14],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bNum= bNum-12-k-pow(k,2);
      index= bNum/pow(k,2);
      offsetInto= bNum%(k*k) ;
      if(blockList[index] !=0){
          fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
          blockList.assign(buffer,buffer+(sizeof(buffer)/sizeof(buffer[0])));
      }
      index= offsetInto/k;
      offsetInto= offsetInto%k;
      if(blockList[index] !=0){
          fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
          blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      }
      //Finally it can be used to locate direct block of the file
      if(blockList[offsetInto]!=0){
        fetchBlock(ext2,blockList[offsetInto],file,mbr,translationMapData,k,buffer);
        return blockList[offsetInto];
      }
  }
}
/*
This function can be used to allocate the blocks
*/
int allocateBlock(struct ext2File *f,struct vdifile *vdi,struct blockGroupDescriptor bg[], uint32_t iBlockNumber,
    int offsetToSuperBlock,int translationMapData[]){
    int readBitBytes,
        blockSize,
        blockGroupNumber,
        offsetBlockBitMap;
    blockSize=1024<<f->superblock.s_log_block_size;
    blockGroupNumber= iBlockNumber/f->superblock.s_blocks_per_group;
    if(blockSize==1024){
      offsetBlockBitMap=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset+
                      (bg[blockGroupNumber].bg_block_bitmap-1-blockGroupNumber*f->superblock.s_blocks_per_group)*(blockSize);
    }
    if(blockSize==4096){
      offsetBlockBitMap=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset-1024+
                      (bg[blockGroupNumber].bg_block_bitmap-blockGroupNumber*f->superblock.s_blocks_per_group)*(blockSize);
    }
    unsigned char blockBitMap[blockSize];
    int physicalAddress= actualPage(offsetBlockBitMap,vdi,translationMapData);
    vdiSeek(vdi,offsetBlockBitMap,SEEK_SET);
    int readBytes=vdiRead(vdi,blockBitMap,sizeof(blockBitMap));
    if(readBytes==1024){
      cout<<"able to read block bitmap"<<endl;
    }
    else{
      cout<<"Unable to read the block bitmap"<<endl;
    }
    bool used= true;
    for(int i=0;i<1024;i++){
      uint8_t oneByte= blockBitMap[i];
      for(int j=0;j<8;j++){
        used= (oneByte>>j & 0x1);
        if(!used){
          f->superblock.s_free_blocks_count -=1;
          bg[blockGroupNumber].bg_free_blocks -=1;
          blockBitMap[i]= (0x1 << (7-j))| blockBitMap[i];
          int blockNumber= i*8+7-j;
          return blockNumber;
        }
      }

    }
    return -1;
}
/*
it is used to write back to the given block number of the file
*/
bool writeBlockToFile(struct inode *i, int bNum,int offsetToGivenInode,int iNum,int blockSize,struct superBlock sBlock,
                     struct ext2File * ext2,struct vdifile*file,struct mbrSector mbr,int translationMapData[],struct blockGroupDescriptor bg[],
                     int offsetToSuperBlock,int * realBuffer){
  vector<int>blockList;
  int newBlockNumber;
  int k= (1024<<sBlock.s_log_block_size)/4;
  int buffer[k],
      index,
      offsetInto;
  if(bNum<12){
      if(i->i_block[bNum]==0){
          i->i_block[bNum]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
          writeInode(ext2,file,bg,iNum,i,offsetToSuperBlock,translationMapData);
      }
      blockList.assign(i->i_block,i->i_block+sizeof(i->i_block)/sizeof(i->i_block[0]));
      bool isDone=writeBlock(ext2,blockList[bNum],file,mbr,translationMapData,realBuffer);
      return isDone;
  }
  else if (bNum<12+k){
      if(i->i_block[12]==0){
        i->i_block[12]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        writeInode(ext2,file,bg,iNum,i,offsetToSuperBlock,translationMapData);
      }
      fetchBlock(ext2,i->i_block[12], file, mbr, translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      newBlockNumber= i->i_block[12];
      bNum= bNum-12;
      index= bNum%k;
      if(blockList[index] ==0){
        blockList[index]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+index)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      newBlockNumber=blockList[index];
      fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bool isDone=writeBlock(ext2,blockList[index],file,mbr,translationMapData,realBuffer);
      return isDone;
  }
  else if(bNum<12+k+pow(k,2)){
      if(i->i_block[13]==0){
        i->i_block[13]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        writeInode(ext2,file,bg,iNum,i,offsetToSuperBlock,translationMapData);
      }
      fetchBlock(ext2,i->i_block[13],file,mbr,translationMapData,k,buffer);
      newBlockNumber=i->i_block[13];
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bNum= bNum-12-k;
      index= offsetInto/k;
      offsetInto= offsetInto%k;
      if(blockList[index] ==0){
        blockList[index]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+index)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      newBlockNumber=blockList[index];
      fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));

      //Finally it can be used to locate direct block of the file
      if(blockList[offsetInto]==0){
        blockList[offsetInto]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+offsetInto)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      bool isDone=writeBlock(ext2,blockList[offsetInto],file,mbr,translationMapData,realBuffer);
      return isDone;
  }
  else{
      if(i->i_block[14]==0){
        i->i_block[14]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        writeInode(ext2,file,bg,iNum,i,offsetToSuperBlock,translationMapData);
      }

      fetchBlock(ext2,i->i_block[14],file,mbr,translationMapData,k,buffer);
      newBlockNumber= i->i_block[14];
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      bNum= bNum-12-k-pow(k,2);
      index= bNum/pow(k,2);
      offsetInto= bNum%(k*k) ;
      if(blockList[index] ==0){
        blockList[index]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+index)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      newBlockNumber=blockList[index];
      fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));
      //In order to access single indirect blocks
      index= offsetInto/k;
      offsetInto= offsetInto%k;
      if(blockList[index] ==0){
        blockList[index]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+index)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      newBlockNumber=blockList[index];
      fetchBlock(ext2,blockList[index],file,mbr,translationMapData,k,buffer);
      blockList.assign(buffer,buffer+sizeof(buffer)/sizeof(buffer[0]));

      //Finally it can be used to locate direct block of the file
      if(blockList[offsetInto]==0){
        blockList[offsetInto]=allocateBlock(ext2,file,bg,bNum+i->i_block[0],offsetToSuperBlock,translationMapData);
        *(buffer+offsetInto)= blockList[index];
        writeBlock(ext2,newBlockNumber,file,mbr,translationMapData,buffer);
      }
      bool isDone=writeBlock(ext2,blockList[offsetInto],file,mbr,translationMapData,realBuffer);
      return isDone;
  }
}
/*
This functioin can be used to write the given read inode to the given block numberOfSectorInPartition
*/
bool writeInode(struct ext2File *f,struct vdifile *vdi,struct blockGroupDescriptor bg[], uint32_t iNum,
               struct inode * in,int offsetToSuperBlock,int translationMapData[]){
    int readInodeBytes,
        blockSize,
        blockGroupNumber,
        offsetToGivenInode;
    blockSize=1024<<f->superblock.s_log_block_size;
    blockGroupNumber= (iNum-1)/f->superblock.s_inodes_per_group;
    if(blockSize==1024){
      offsetToGivenInode=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset+
                       (bg[blockGroupNumber].bg_inode_table-1-blockGroupNumber*f->superblock.s_blocks_per_group)*
                       (blockSize)+((iNum-1)%f->superblock.s_inodes_per_group)*f->superblock.s_inode_size;
    }
    if(blockSize==4096){
      offsetToGivenInode=blockGroupNumber*(blockSize)* f->superblock.s_blocks_per_group+offsetToSuperBlock-vdi->header.frameOffset-1024+
                       (bg[blockGroupNumber].bg_inode_table-blockGroupNumber*f->superblock.s_blocks_per_group)*
                       (blockSize)+((iNum-1)%f->superblock.s_inodes_per_group)*f->superblock.s_inode_size;
    }
    int physicalAddress= actualPage(offsetToGivenInode,vdi,translationMapData);
    vdiSeek(vdi,physicalAddress,SEEK_SET);
    int bytesWritten=write(vdi->fileDescriptor,in,sizeof(struct inode));
    if(bytesWritten ==sizeof(struct inode)){
      return true;
    }
    return false;
}
/*
Extra function that can be used to display the data related to the inode
*/
void displayInode(struct inode in){
  cout<<"\n"<<"Mode : "<<std::oct<<in.i_mode<<"\n";
  cout<<"Size : "<<std::dec<<in.i_size<<"\n";
  cout<<"Blocks : "<<std::dec<<in.i_blocks<<"\n";
  cout<<"UID/GID : "<<std::dec<<in.i_uid<<"\n";
  cout<<"Links : "<<std::dec<<in.i_links_count<<"\n";
  cout<<"Last created : "<<std::dec<<in.i_ctime<<" Seconds since 1970 janurary 1st"<<"\n";
  cout<<"Last access : "<<std::dec<<in.i_atime<<"  Seconds since 1970 janurary 1st"<<"\n";
  cout<<"Last Modification : "<<std::dec<<in.i_mtime<<"  Seconds since 1970 janurary 1st"<<"\n";
  cout<<"Deleted : "<<std::dec<<in.i_dtime<<"  Seconds since 1970 janurary 1st"<<"\n";
  cout<<"Flags : "<<std::dec<<in.i_flags<<"\n";
  cout<<" File version : "<<std::dec<<in.i_generation<<"\n";
  cout<<"ACL blocks : "<<std::dec<<in.i_file_acl<<"\n";
  cout<<"Direct Blocks: "<<"\n";
  cout<<"0-3 : "<<"\n";
  for(int i=0;i<4;i++){
    cout<<std::dec<<in.i_block[i]<<" ";
  }
  cout<<"\n"<<"4-7 : "<<"\n";
  for(int i=4;i<8;i++){
    cout<<std::dec<<in.i_block[i]<<" ";
  }
  cout<<"\n"<<"8-11 : "<<"\n";
  for(int i=8;i<12;i++){
    cout<<std::dec<<in.i_block[i]<<" ";
  }
  cout<<"\n"<<"Single Indirect Block : "<<std::dec<<in.i_block[12]<<"\n";
  cout<<"Double Indirect Block : "<<std::dec<<in.i_block[13]<<"\n";
  cout<<"Triple Indirect Block : "<<std::dec<<in.i_block[14]<<"\n";
}
/*
This function can be used to write to blocks
*/
bool writeBlock(struct ext2File * ext2,int blockNumber,struct vdifile*file,struct mbrSector mbr,int translationMapData[],int *buffer){
  int offsetToGivenBlock= mbr.partitionEntryInfo[0].logicalBlocking*512+blockNumber*blockNumber*1024;
  int physicalAddress= actualPage(offsetToGivenBlock,file,translationMapData);
  vdiSeek(file,physicalAddress,SEEK_SET);
  int writeBytes= write(file->fileDescriptor,buffer,1024);
  if(writeBytes== 1024){
    cout<<"Successfully written"<<endl;
    return true;
  }
  else{
    cout<<"Unable to write successfully"<<endl;
    return false;
  }

}
