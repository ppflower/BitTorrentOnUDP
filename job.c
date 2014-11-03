#include "job.h"


extern bt_config_t config;
extern job_t job;
extern queue_t* hasChunk;


static const char *type2str[] = { "WHOHAS",
                                  "IHAVE",
                                  "GET",
                                  "DATA",
                                  "ACK",
                                  "DENIED" };

/** @brief Initilize a job
 *  @param The address of the file which contains chunk id and hash code
 *  @return 0 if job initilization is successed
 *          1 if failed
 */
int init_job(char* chunkFile, char* output_file) {
    FILE* file = fopen(chunkFile,"r");
    if( file == NULL)
        return -1; // fail to open job file

    int line_number = 0;
    int i = 0, j;
    char read_buffer[BUF_SIZE];
    char hash_buffer[SHA1_HASH_SIZE*2];
    chunk_t* this_chunk;

    
    /* get chunks number */
    while (fgets(read_buffer, BUF_SIZE,file)) {
        line_number++;
    }
    memset(read_buffer,0,BUF_SIZE);
    
    job.num_chunk = line_number;
    job.num_need = line_number;
    job.num_living = 0;
    job.chunks = malloc(sizeof(chunk_t) * job.num_chunk);
    
    /* set ptr to the beginning */
    fseek(file,0,SEEK_SET);
    
    while (fgets(read_buffer,BUF_SIZE,file)) {
        this_chunk = &(job.chunks[i]);
        sscanf(read_buffer,"%d %s",&(job.chunks[i].id),hash_buffer);
        /* convert ascii to binary hash code */
        hex2binary(hash_buffer,SHA1_HASH_SIZE*2,job.chunks[i].hash);        
        memset(read_buffer,0,BUF_SIZE);
        memset(hash_buffer,0,SHA1_HASH_SIZE*2);
        this_chunk->pvd = NULL;
        this_chunk->num_p = 0;
        this_chunk->cur_size = 0;
        this_chunk->data = malloc(sizeof(char)*512*1024);
        for (j = 0; j < BLOCK_SIZE; j++) {
            this_chunk->block_map[j] = 0;    
        }
        i++;
    }    
    fclose(file);
    // set output file address and format
    strcpy(config.output_file,output_file);
    strcpy(job.get_chunk_file,chunkFile);
    config.output_file[strlen(output_file)] = '\0';
    job.get_chunk_file[strlen(job.get_chunk_file)] = '\0';
    job.cwnd = fopen("./problem2-peer.txt", "a+");

    //gettimeofday(&(job.start_time), NULL);
    //fprintf(job.cwnd, "Start!\n");
    
    // successfully initilize job
    return 0;
}

void clear_job() {
    job.num_chunk = 0; 
    job.num_need  = 0;
    job.cwnd = NULL;
    job.chunks = NULL;
}

int is_job_finished() {
    return job.num_need == 0;    
}

int packet_parser(char* buf) {

    data_packet_t* pkt = (data_packet_t*) buf;
    header_t *hdr = &pkt->header;
    /* check magic number */
    if(hdr->magicnum != 15441)
        return -1;
    if(hdr->version != 1)
        return -1;
    if(hdr->packet_type < 0 || hdr->packet_type > 5)
        return -1;
    return hdr->packet_type;

}

/** @brief Generate WhoHas package
 *  @return NULL if current job.num_chunk is 0
 *          a queue of pkt if job.num_chunk is not 0
 */
queue_t *WhoHas_maker() {
    // to do parse getchunkfile
    queue_t *q = queue_init();
    data_packet_t *pkt;
    char data[DATALEN];
    short pkt_len;
    int i;  // loop index
    int n;  /* multiple of 74 */
    int m;  /* number of chunks can fit into one pkt */

    if (0 == job.num_chunk) {
        free_queue(q);
        return NULL;
    } else if (job.num_chunk > MAX_CHUNK) {
        n = job.num_chunk / MAX_CHUNK; /* multiple of 74 */
        for (i = 0; i < n; i++) {
            pkt_len = HEADERLEN + 4 + MAX_CHUNK * SHA1_HASH_SIZE;
            whohas_data_maker(MAX_CHUNK, job.chunks + i * MAX_CHUNK, data);
            pkt = packet_maker(PKT_WHOHAS, pkt_len, 0, 0, data);
            enqueue(q, (void *)pkt);
        }
        m = job.num_chunk - n * MAX_CHUNK; /* number of chunks can fit into one pkt */
        pkt_len = HEADERLEN + 4 + m * SHA1_HASH_SIZE;
        whohas_data_maker(m, job.chunks + n * MAX_CHUNK, data);
        pkt = packet_maker(PKT_WHOHAS, pkt_len, 0, 0, data);
        enqueue(q, (void *)pkt);
    } else {
        pkt_len = HEADERLEN + 4 + job.num_chunk * SHA1_HASH_SIZE;
        whohas_data_maker(job.num_chunk, job.chunks, data);
        pkt = packet_maker(PKT_WHOHAS, pkt_len, 0, 0, data);
        enqueue(q, (void *)pkt); 
    }
    return q;
}

/** @brief Generate WhoHas data part
 *  @param num_chunk the number of chunk requested
 *  @param chunks chunk_t that contian hash
 *  @param data an space to fillint data
 *  @return void
 */
void whohas_data_maker(int num_chunk, chunk_t *chunks, char* data) {
    char *ptr;
    short pkt_len;
    int num_need = 0;  // the number of chunk needs download
    int i;
    
    /* Header:16 + num,padding:4 + hashs:20*num of chunk */
    pkt_len = HEADERLEN + 4 + num_chunk * SHA1_HASH_SIZE;    
    memset(data, 0, 4); /* padding 3 bytes */
    ptr = data + 4;        /* shift 4 to fill in chunk hashs */
    for (i = 0; i < num_chunk; i++) {
        /* Chunk Hash 20 bytes */
        if (chunks[i].cur_size == 512*1024) // I have this chunk already
            continue;
        num_need++;
        memcpy(ptr + i * SHA1_HASH_SIZE, chunks[i].hash, SHA1_HASH_SIZE);
    }
    data[0] = num_need; /* Number of chunks */
}

/** @brief Generate IHave data part
 *  @param whohas_pkt the recieved WhoHas pkt
 *  @return a pointer to data_packet_t generated
 */
data_packet_t *IHave_maker(data_packet_t *whohas_pkt) {
    // how many chunks are requested
    // how many of them i have
    // make IHave packet
    // return Ihave packet
    int req_num;
    int data_length = 4; // length of data including num and padding
    int have_num = 0;   // the num of chk I have 
    int i;
    char rawdata[PACKETLEN];
    uint8_t *hash_start;
    data_packet_t *pkt;

    assert(whohas_pkt->header.packet_type == PKT_WHOHAS);

    req_num = whohas_pkt->data[0];
    hash_start = (uint8_t *)(whohas_pkt->data + 4);
    for (i = 0; i < req_num; i++) {
        if (IfIHave(hash_start)) {            
            have_num++;
            memcpy(rawdata+data_length, hash_start, SHA1_HASH_SIZE);
            data_length += SHA1_HASH_SIZE;
        }
        hash_start += SHA1_HASH_SIZE;
    }
    if (0 == have_num)
        return NULL;  // I dont have any of you request chk
    
    memset(rawdata, 0, 4);
    rawdata[0] = have_num;
    pkt = packet_maker(PKT_IHAVE, HEADERLEN + data_length, 0, 0, rawdata);
    return pkt;
}


/** @brief IF I have the requested chunk
 *  @param hash_start requested hash that I need to cmp with what I have
 *  @return 1 if I have some of them
 *          0 if I dont have any one
 */
int IfIHave(uint8_t *hash_start) {
    int i, num;
    node_t *node;
    chunk_t* this_chunk;
    if (hasChunk->n == 0)
        return 0;
    node = hasChunk->head;
    num = hasChunk->n;
    for (i = 0; i < num; i++) {
        this_chunk = (chunk_t *)node->data;
        if (memcmp(hash_start, this_chunk->hash, SHA1_HASH_SIZE)) {
            node = node->next;
            continue;
        }                
        return 1;
    }
    return 0;
}


/** @brief Generate GET pkt
 *  @param pkt incoming Ihave pkt
 *  @param provider the incoming peer who send the IHave
 *  @param chunk list which this connection refer to 
 *  @return NULL I dont need to send GET this provider.
 *          a queue of GET pkt
 */ 
queue_t* GET_maker(data_packet_t *ihave_pkt, bt_peer_t* provider, queue_t* chunk_queue) {
    assert(ihave_pkt->header.packet_type == PKT_IHAVE);
    int num = ihave_pkt->data[0]; // num of chunk that peer has
    //int num_match = 0;
    int i;
    int match_idx;
    chunk_t* chk = job.chunks;  // the needed chunk here
    queue_t *q;      // the queue of GET request
    data_packet_t* pkt; // GET packet
    uint8_t *hash;   // the incoming hash waiting to match my needs
    if (0 == num)
        return NULL;

    q = queue_init();
    hash = (uint8_t *)(ihave_pkt->data + 4); // the start of hash
    for (i = 0; i < num; i++) {
        match_idx = match_need(hash);
        if (-1 != match_idx) {
            chk[match_idx].pvd = provider;
            chk[match_idx].num_p = 1;
            job.num_living |= (1 << match_idx);   // this chunks is living
            pkt = packet_maker(PKT_GET, HEADERLEN + SHA1_HASH_SIZE, 0, 0, (char *)hash);
            enqueue(q, (void *)pkt);
            enqueue(chunk_queue,(void*)(chk+match_idx));
        }
        hash += SHA1_HASH_SIZE;
    }
    return q;
}

/** @brief Generate data pkt array
 *  @param pkt incoming get pkt 
 *  @return NULL if failed to generate pkt.
 *          data packet array 
 */
data_packet_t** DATA_pkt_array_maker(data_packet_t* pkt) {
    data_packet_t** data_pkt_array = (data_packet_t**)malloc(512*sizeof(data_packet_t*));
    int index = 0, i = 0;
    char hash_buffer[HASH_HEX_SIZE] = {0};
    char hash_hex[HASH_HEX_SIZE] = {0};
    char buffer[BT_FILENAME_LEN+5] = {0};
    char datafile[BT_FILENAME_LEN] = {0};
    char index_buffer[5] = {0};
    char *src;
    struct stat statbuf;

    FILE* index_file = fopen(config.chunk_file,"r");
    int data_fd;

    if(index_file == NULL) {
        fprintf(stderr, "Fail to open chunk file!!\n"); 
        return NULL;
    }
    // get data file address
    fgets(buffer,BT_FILENAME_LEN,index_file);

    sscanf(buffer,"File: %s\n",datafile);
    // skip the next line
    fgets(buffer,BT_FILENAME_LEN,index_file);

    // open file to read 
    data_fd = open(datafile, O_RDONLY);
    fstat (data_fd, &statbuf);
    src = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, data_fd, 0);
    close(data_fd);
    binary2hex(pkt->data,SHA1_HASH_SIZE,hash_hex);
    while(fgets(buffer,60,index_file) != NULL) {
        if(sscanf(buffer,"%s %s\n",index_buffer,hash_buffer) < 2 ) {
            // wrong file format!
            fprintf(stderr, "wrong file format!\n");
            fclose(index_file);
            munmap(src,statbuf.st_size);
            return NULL;
        } else {
            if(memcmp(hash_hex,hash_buffer,HASH_HEX_SIZE) == 0) {
                index = atoi(index_buffer);
                //fseek(data_file,index,SEEK_SET);
                for (i = 0;i < 512;i++) {
                    // load data
                    //fread(data_buffer,1,1024,data_file);
                    data_pkt_array[i] = packet_maker(PKT_DATA,1040,i+1,0,src+index*CHUNK_SIZE+i*1024);
                    //memset(data_buffer,0,1024);
                }
                munmap(src,statbuf.st_size);
                print_pkt((data_packet_t*)(data_pkt_array[0]));
                return data_pkt_array;
            }
        }
    }
    return NULL;
}

/** @brief Generate ACK pkt
 *  @param the last biggest ack number
 *  @param pkt incoming data pkt 
 *  @return NULL if failed to generate pkt.
 *          an ACK packet
 */
data_packet_t* ACK_maker(int ack, data_packet_t* pkt) {
    assert(pkt->header.packet_type == PKT_DATA);
    data_packet_t* ack_pkt = packet_maker(PKT_ACK, HEADERLEN, 0, ack-1, NULL);
    return ack_pkt;
}

/** @brief Generate DENIED pkt
 *  @return NULL if failed to generate pkt.
 *          an ACK packet
 */
data_packet_t* DENIED_maker() {
    data_packet_t* denied_pkt = packet_maker(PKT_DENIED, HEADERLEN, 0, 0, NULL);
    return denied_pkt;
}

/** @brief try to match the incoming IHave to what I want
 *  @param hash the incoming chunk hash to be matched
 *  @return -1 if I dont need this
 *          the int id of needed chunk that matches this hash
 */
int match_need(uint8_t *hash) {
    int i;
    chunk_t* chk = job.chunks;
    if (0 == job.num_chunk)
        return -1;
    for (i = 0; i < job.num_chunk; i++) {
        if (NULL != chk[i].pvd)
            continue;
        if (memcmp(hash, chk[i].hash, SHA1_HASH_SIZE)) {
            continue;
        } else return i;        
    }
    return -1;
}


/** @brief Generate package
 *  @param type the type of pkt WHOHAS, IHAVE, GET, DATA, ACK, DENIED
 *  @param pkt_len the length of pkt including header
 *  @param seq sequence num
 *  @param ack ack num
 *  @param data the pointer to data (memcpy needed)
 *  @return pointer to the pkt genereted
 */
data_packet_t *packet_maker(int type, short pkt_len, u_int seq, u_int ack, char *data) {
    data_packet_t *pkt = (data_packet_t *)malloc(sizeof(data_packet_t));
    pkt->header.magicnum = 15441; /* Magic number */
    pkt->header.version = 1;      /* Version number */
    pkt->header.packet_type = type; /* Packet Type */
    pkt->header.header_len = HEADERLEN;    /* Header length is always 16 */
    pkt->header.packet_len = pkt_len;
    pkt->header.seq_num = seq;
    pkt->header.ack_num = ack;
    if( pkt->data != NULL) 
        memcpy(pkt->data, data, pkt_len - HEADERLEN);
    return pkt;
}

/** @brief Send whohas pkt to all peers in the network
 *  @param pkt pkt to be send
 *  @return void
 */
void send_WhoHas(data_packet_t* pkt) {
    char str[20];
    struct bt_peer_s* peer = config.peers;

    //bt_dump_config(&config);
    while(peer != NULL) {
        fprintf(stderr, "ID:%d\n", peer->id);
        fprintf(stderr, "Port:%d\n", ntohs(peer->addr.sin_port));
        inet_ntop(AF_INET, &(peer->addr.sin_addr), str, INET_ADDRSTRLEN);
        fprintf(stderr, "IP:%s\n", str);
        if (peer->id != config.identity) {
            packet_sender(pkt,(struct sockaddr *) &peer->addr);
        }
        peer = peer->next;
    }
}


void flood_WhoHas() {
    if (VERBOSE)
        fprintf(stderr, "Entering Flood WhoHas!\n");
    
    if (job.num_living == ((1 << (job.num_chunk + 1)) - 1)) {
        if (VERBOSE)
            fprintf(stderr, "All chunks are living!\n");
        return;
    }
    /* call whohasmaker */
    queue_t* whoHasQueue = WhoHas_maker();
    /* send out all whohas packets */
    data_packet_t* cur_pkt = NULL;
    while((cur_pkt = (data_packet_t *)dequeue(whoHasQueue)) != NULL) {
        //fprintf(stderr, "here\n");
        send_WhoHas(cur_pkt);
        packet_free(cur_pkt);
    }
    free_queue(whoHasQueue);
}


void packet_sender(data_packet_t* pkt, struct sockaddr* to) {
    int pkt_size = pkt->header.packet_len;
    int type = pkt->header.packet_type;
    if (VERBOSE)
        fprintf(stderr, "send %s pkt!*********\n", type2str[type]);
    print_pkt(pkt);
    hostToNet(pkt);
    spiffy_sendto(config.sock, pkt, pkt_size, 0, to, sizeof(*to));
    netToHost(pkt);
}

void store_data(chunk_t* chunk, data_packet_t* pkt) {
    int size = pkt->header.packet_len - pkt->header.header_len;
    int seq_num = pkt->header.seq_num;
    memcpy(chunk->data+(seq_num - 1) * 1024,pkt->data,size);
    chunk->cur_size += size;
    chunk->block_map[seq_num-1] = 1;

}

void update_lst_ask(down_conn_t* down_conn) {
    chunk_t* cur_chunk = (chunk_t*)(down_conn->chunks->head->data);
    int i;
    for (i = (down_conn->next_pkt - 1); i < BLOCK_SIZE; i++) {
        if (cur_chunk->block_map[i] == 1)
            continue;
        else
            down_conn->next_pkt = i + 1;
    }
}


void cat_chunks() {
    FILE* fdout;
    int i;
    int num_chk = job.num_chunk;
    int size = num_chk * CHUNK_SIZE;
    chunk_t *chk_arr = job.chunks;

    assert(job.num_need == 0);


    fdout = fopen(config.output_file, "w");
    for (i = 0; i < num_chk; i++) {
        fwrite(chk_arr[i].data,1,CHUNK_SIZE,fdout);
    }
    fprintf(stderr, "cat finished!!!\n");
    fclose(fdout);
}


/** @brief check if a chunk has been fully downloaded
 *  @param test chunk
 *  @return 1 for finished, 0 for not finished
 */
int is_chunk_finished(chunk_t* chunk) {
    int cur_size = chunk->cur_size;
    if (VERBOSE)
        fprintf(stderr, "check finished!!!\n");
    if (cur_size != CHUNK_SIZE) {
        if (VERBOSE)
            fprintf(stderr, "Not finished yet, cur_size = %d\n", (cur_size>>10));
        return 0;
    }
    uint8_t hash[SHA1_HASH_SIZE];
    // get hash code
    shahash(chunk->data,cur_size,hash);
    // check hash code
    if (memcmp(hash,chunk->hash,SHA1_HASH_SIZE) == 0) {
        return 1;
    } else {
        return 0;
    }
}

/** @brief Convert data from local format to network format
 *  @param pkt pkt to be send
 *  @return void
 */
void hostToNet(data_packet_t* pkt) {
    pkt->header.magicnum = htons(pkt->header.magicnum);
    pkt->header.header_len = htons(pkt->header.header_len);
    pkt->header.packet_len = htons(pkt->header.packet_len);
    pkt->header.seq_num = htonl(pkt->header.seq_num);
    pkt->header.ack_num = htonl(pkt->header.ack_num);
}

/** @brief Convert data from network format to local format
 *  @param pkt pkt to be send
 *  @return void
 */
void netToHost(data_packet_t* pkt) {
    pkt->header.magicnum = ntohs(pkt->header.magicnum);
    pkt->header.header_len = ntohs(pkt->header.header_len);
    pkt->header.packet_len = ntohs(pkt->header.packet_len);
    pkt->header.seq_num = ntohl(pkt->header.seq_num);
    pkt->header.ack_num = ntohl(pkt->header.ack_num);
}

/** @brief free pkt
 *  @param pkt the pkt to be freed
 *  @return void
 */
void packet_free(data_packet_t *pkt) {
    //free(pkt->data);
    free(pkt);
}

/** @brief Print out the given pkt
 *  @param pkt the pkt to to printed out
 *  @return void
 */
void print_pkt(data_packet_t* pkt) {
    if (VERBOSE != 1)
        return;
    header_t* hdr = &pkt->header;
    uint8_t* hash;
    int num;
    int i;
    fprintf(stderr, ">>>>>>>>>START<<<<<<<<<<<<<\n");
    fprintf(stderr, "magicnum:\t\t%d\n", hdr->magicnum);
    fprintf(stderr, "version:\t\t%d\n", hdr->version);
    fprintf(stderr, "packet_type:\t\t%d\n", hdr->packet_type);
    fprintf(stderr, "header_len:\t\t%d\n", hdr->header_len);
    fprintf(stderr, "packet_len:\t\t%d\n", hdr->packet_len);
    fprintf(stderr, "seq_num:\t\t%d\n", hdr->seq_num);
    fprintf(stderr, "ack_num:\t\t%d\n", hdr->ack_num);
    if (PKT_WHOHAS == hdr->packet_type || PKT_IHAVE == hdr->packet_type) {
        num = pkt->data[0];
        fprintf(stderr, "1st bytes data:\t\t%x\n", pkt->data[0]);
        hash = (uint8_t *)(pkt->data + 4);
        for (i = 0; i < num; i++) {
            print_hash(hash);
            hash += SHA1_HASH_SIZE;
        }
    }
    fprintf(stderr, ">>>>>>>>>END<<<<<<<<<<<<<\n");
}


/** @brief Print out hash
 *  @param hash the pointer to the hash to be printed out
 *  @return void
 */
void print_hash(uint8_t *hash) {
    int i;
    for (i = 0; i < SHA1_HASH_SIZE;) {
        fprintf(stderr, "%02x", hash[i++]);
        if (!(i % 4))
            fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");
}



void freeJob() {    
    /* free each chunks */
    free(job.chunks);
    job.chunks = NULL;
    job.num_chunk = 0;
}



