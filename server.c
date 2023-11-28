#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h> 
#include <rdma/rdma_verbs.h>

enum { 
    RESOLVE_TIMEOUT_MS = 10000,
};

struct pdata { 
    uint64_t buf_va; 
    uint32_t buf_rkey; 
};

int main(int argc, char *argv[]) 
{ 
    printf("enter main:\n");
    struct pdata    rep_pdata;

    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *listen_id; 
    struct rdma_cm_id           *cm_id; 
    struct rdma_cm_event        *event; 
    struct rdma_conn_param      conn_param = { };

    struct ibv_pd           *pd; 
    struct ibv_comp_channel *comp_chan; 
    struct ibv_cq           *cq;  
    struct ibv_cq           *evt_cq;
    struct ibv_mr           *mr; 
    struct ibv_qp_init_attr qp_attr = { };
    struct ibv_sge          sge; 
    struct ibv_send_wr      send_wr = { };
    struct ibv_send_wr      *bad_send_wr; 
    struct ibv_recv_wr      recv_wr = { };
    struct ibv_recv_wr      *bad_recv_wr;
    struct ibv_wc           wc;
    void                    *cq_context;

    struct sockaddr_in      sin;

    uint32_t                *buf;
    
    int                     err;

    /* Set up RDMA CM structures */
    
    cm_channel = rdma_create_event_channel();
    if (!cm_channel) 
        return 1;

    err = rdma_create_id(cm_channel, &listen_id, NULL, RDMA_PS_TCP); 
    if (err) {
        printf("rdma_create error");
        return err;
    }

    sin.sin_family = AF_INET; 
    sin.sin_port = htons(20079);
    sin.sin_addr.s_addr = inet_addr("128.110.218.92");

    
    /* Bind to local port and listen for connection request */

    err = rdma_bind_addr(listen_id, (struct sockaddr *) &sin);
    if (err) 
        return 1;

    err = rdma_listen(listen_id, 1);
    if (err)
        return 1;

    err = rdma_get_cm_event(cm_channel, &event);
    if (err){
        printf("rdma_get cm error");
        return err;
    }

    printf("%d", event -> event);
    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) // event returns 4. 
        return 1;

    cm_id = event->id;
    printf("The value of id is: %d\n", event->id); //event if its a connect request, we get that ID.

    int res = rdma_ack_cm_event(event);
    printf("%d",res);

    /* Create verbs objects now that we know which device to use */
    
    pd = ibv_alloc_pd(cm_id->verbs);
    if (!pd) 
        return 1;

    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    if (!comp_chan)
        return 1;

    cq = ibv_create_cq(cm_id->verbs, 2, NULL, comp_chan, 0); 
    if (!cq)
        return 1;

    if (ibv_req_notify_cq(cq, 0))
        return 1;

    buf = calloc(2, sizeof (uint32_t));
    if (!buf) 
        return 1;

   mr = ibv_reg_mr(pd, buf, 2 * sizeof (uint32_t), 
        IBV_ACCESS_LOCAL_WRITE | 
        IBV_ACCESS_REMOTE_READ | 
        IBV_ACCESS_REMOTE_WRITE); 
    if (!mr) 
        return 1;
    
    qp_attr.cap.max_send_wr = 1;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 1;
    qp_attr.cap.max_recv_sge = 1;

    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;

    qp_attr.qp_type = IBV_QPT_RC;
    
    err = rdma_create_qp(cm_id, pd, &qp_attr); 
    if (err) {
        printf("rdma_create qp error");
        return err;
    }

    /* Post receive before accepting connection */
    sge.addr = (uintptr_t) buf + sizeof (uint32_t); 
    sge.length = sizeof (uint32_t); 
    sge.lkey = mr->lkey;

    recv_wr.sg_list = &sge; 
    recv_wr.num_sge = 1;

    if (ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr))
        return 1;

    rep_pdata.buf_va = htonl((uintptr_t) buf); 
    rep_pdata.buf_rkey = htonl(mr->rkey); 

    conn_param.responder_resources = 1;  
    conn_param.private_data = &rep_pdata; 
    conn_param.private_data_len = sizeof rep_pdata;

    /* Accept connection */

    err = rdma_accept(cm_id, &conn_param); 
    if (err) 
        return 1;

    printf("connection accepted:\n");
    err = rdma_get_cm_event(cm_channel, &event);
    if (err) {
        printf("rdma_get cm error\n");
        return err;
    }

    if (event->event != RDMA_CM_EVENT_ESTABLISHED)
        return 1;


    
    // rdma_ack_cm_event(event);
    printf("receive completion\n");

    int check = ibv_get_cq_event(comp_chan, &evt_cq, &cq_context);
    printf("%d\n",check); // success 
    printf("%d\n",wc.status);

    /* Wait for receive completion */
    
    if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context)) {
    printf("ibv_get_cq");
    return 1;  // on success we return 0
    }   
    
    int check2 = ibv_req_notify_cq(cq, 0);
    printf("Check2 : %d\n",check2);
    if (ibv_req_notify_cq(cq, 0)){
        printf("error in cq notif");
        return 1;
    }

    // int check2 = ibv_req_notify_cq(cq, 0);
    // printf("%d\n",check2);

    if (ibv_poll_cq(cq, 1, &wc) < 1){
        printf("ibv poll error");
        return 1;
    }
    
    printf("Line 200 status: %d\n",wc.status);
    if (wc.status != IBV_WC_SUCCESS) //spark error
        return 1;

    /* Add two integers and send reply back */

    printf("before add");
    buf[0] = htonl(ntohl(buf[0]) + ntohl(buf[1]));
    printf("after add");

    sge.addr = (uintptr_t) buf;  
    sge.length = sizeof (uint32_t); 
    sge.lkey = mr->lkey;
    
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1 ;

    /* Wait for receive completion */

    if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context)) {
        printf("ibv_get_cq");
        return 1;
    }

    ibv_ack_cq_events(evt_cq, 1);

    if (ibv_req_notify_cq(cq, 0))
        return 1;

    if (ibv_poll_cq(cq, 1, &wc) < 1)
        return 1;

    if (wc.status != IBV_WC_SUCCESS) //spark error
        return 1;


    // ibv_ack_cq_events(cq, 2);
    return 0;
}

