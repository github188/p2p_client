//
//  ViewController.m
//  p2pdemo
//
//  Created by yhzl on 15-3-17.
//  Copyright (c) 2015年 yhzl. All rights reserved.
//

#import "ViewController.h"
#import "p2p_transport.h"
#import "p2p_dispatch.h"

@interface ViewController ()
{
@public
    p2p_transport* p2pTransport;
    int p2pConn;
    int p2pStatus;
    unsigned short p2pLocalListenPort;
    
    NSString* ds_result_server;
    int ds_result_port;
    int ds_result_svr_id;
}

@end

static void on_connection_disconnect(p2p_transport *transport,
                                     int connection_id,
                                     void *transport_user_data,
                                     void *connect_user_data)
{
    ViewController* vc = (__bridge ViewController *)(transport_user_data);
    [vc performSelectorOnMainThread:@selector(onDisconnectConnection) withObject:nil waitUntilDone:NO];
}

static void on_create_complete(p2p_transport *transport,
                               int status,
                               void *user_data)
{
    ViewController* vc = (__bridge ViewController *)(user_data);
    vc->p2pStatus = status;
    [vc performSelectorOnMainThread:@selector(onCreateComplete) withObject:nil waitUntilDone:NO];
}

static void on_connect_complete(p2p_transport *transport,
                                int connection_id,
                                int status,
                                void *transport_user_data,
                                void *connect_user_data)
{
    ViewController* vc = (__bridge ViewController *)(transport_user_data);
    vc->p2pStatus = status;
    [vc performSelectorOnMainThread:@selector(onConnectComplete) withObject:nil waitUntilDone:NO];
}

static void on_accept_remote_connection(p2p_transport *transport,
                                        int connection_id,
                                        int conn_flag, 
                                        void *transport_user_data)
{
    ViewController* vc = (__bridge ViewController *)(transport_user_data);
    [vc performSelectorOnMainThread:@selector(onAcceptConnection) withObject:nil waitUntilDone:NO];
}

void open_debug_file(const char* name, FILE ** f, const char* mode)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [paths objectAtIndex:0];
    NSString *strName = [NSString stringWithFormat:@"%s", name];
    
    NSString *writablePath = [documentsDirectory stringByAppendingPathComponent:strName];
    
    unsigned long len = [writablePath length];
    
    char *filepath = (char*)malloc(sizeof(char) * (len + 1));
    
    [writablePath getCString:filepath maxLength:len + 1 encoding:[NSString defaultCStringEncoding]];
    
    *f = fopen(filepath, mode);
}

static void on_connection_recv(p2p_transport *transport,
                               int connection_id,
                               void *transport_user_data,
                               void *connect_user_data,
                               char* data,
                               int len)
{
    /*FILE* f = 0;
    open_debug_file("p2precv", &f, "ab");
    if(f)
    {
        fwrite(data, sizeof(char), len, f);
        fclose(f);
    }*/
}

static void on_dispatch_result(void* dispatcher, int status, void* user_data, char* server, unsigned short port, unsigned int server_id)
{
    ViewController* vc = (__bridge ViewController *)(user_data);
    vc->p2pStatus = status;
    vc->ds_result_port = port;
    vc->ds_result_svr_id = server_id;
    if(status == 0)
        vc->ds_result_server = [NSString stringWithCString:server encoding:NSUTF8StringEncoding];
    [vc performSelectorOnMainThread:@selector(onDispatchResult) withObject:nil waitUntilDone:NO];
    destroy_p2p_dispatch_requester(dispatcher);
}

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    
    p2pTransport = 0;
    p2pConn = 0;
    p2pLocalListenPort = 0;
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (BOOL)checkInput
{
 
    if(_textFieldServer.text.length == 0){
        [self addStateText:@"server is empty!"];
        return NO;
    }
    
    if(_textFieldPort.text.length == 0){
        [self addStateText:@"TURN port is empty!"];
        return NO;
    }
    int port = [_textFieldPort.text intValue];
    if(port <= 0 || port >65535){
        [self addStateText:@"invalid TURN port!"];
        return NO;
    }
    
    if(_textFieldDSPort.text.length == 0){
        [self addStateText:@"DS port is empty!"];
        return NO;
    }
    port = [_textFieldDSPort.text intValue];
    if(port <= 0 || port >65535){
        [self addStateText:@"invalid DS port!"];
        return NO;
    }
    
    return YES;
}

- (IBAction)ConnectServer:(id)sender {
    
    if(![self checkInput])
        return;
    
    if( p2pTransport !=0 ){
        [self addStateText:@"p2pTransport !=0 !"];
        return ;
    }
    int port = [_textFieldPort.text intValue];
    p2p_transport_cfg cfg;
 
    memset(&cfg, 0, sizeof(cfg));
    
    if(ds_result_server)
         cfg.server = (char*)[ds_result_server UTF8String];
    else
        cfg.server = (char*)[_textFieldServer.text UTF8String];
    cfg.port = port;
    cfg.user = (char*)[_textFieldUser.text UTF8String];;
    cfg.password = (char*)[_textFieldPassword.text UTF8String];
    cfg.user_data = (__bridge void *)(self);
    
    if(cfg.user && cfg.user[0])
        cfg.terminal_type = P2P_DEVICE_TERMINAL;
    else
        cfg.terminal_type = P2P_CLIENT_TERMINAL;
    
    p2p_transport_cb cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_connect_complete = on_connect_complete;
    cb.on_create_complete = on_create_complete;
    cb.on_connection_disconnect = on_connection_disconnect;
    cb.on_accept_remote_connection = on_accept_remote_connection;
    cb.on_connection_recv = on_connection_recv;
    cfg.cb = &cb;
    
    p2p_log_set_level(6);
    int status = p2p_transport_create(&cfg, &p2pTransport);
    NSString *string = [NSString stringWithFormat:@"p2p_transport_create return %d", status];
    [self addStateText:string];
}

- (IBAction)DisconnectServer:(id)sender {
    
    if( p2pTransport !=0 ){
        p2p_transport_destroy(p2pTransport);
        [self addStateText:@"disconnect server ok"];
        return;
    }
    else{
        [self addStateText:@"p2pTransport is destroyed"];
        return;
    }
}

- (IBAction)ConnectUser:(id)sender {
    if(_textFieldRemoteUser.text.length == 0){
        [self addStateText:@"remote user is empty!"];
        return;
    }
    if( p2pTransport==0 ){
        [self addStateText:@"p2pTransport is 0 !"];
        return;
    }
    if( p2pConn != 0 ){
        [self addStateText:@"p2pConn != 0 !"];
        return;
    }
    
    int status = p2p_transport_connect(p2pTransport,
                                       (char*)[_textFieldRemoteUser.text UTF8String],
                                       0,
                                       0,
                                       &p2pConn);
    NSString *string = [NSString stringWithFormat:@"p2p_transport_connect return %d", status];
    [self addStateText:string];
    
}

- (IBAction)DisconnectUser:(id)sender {
    if( p2pTransport==0 ){
        [self addStateText:@"p2pTransport is 0 !"];
        return;
    }
    if( p2pConn == 0 ){
        [self addStateText:@"p2pConn == 0 !"];
        return;
    }
    
    p2p_transport_disconnect(p2pTransport, p2pConn);
    [self addStateText:@"disconnect remote user ok"];
    return;
}

- (IBAction)CreateProxy:(id)sender {
    if( p2pTransport==0 ){
        [self addStateText:@"p2pTransport is 0 !"];
        return;
    }
    if( p2pConn == 0 ){
        [self addStateText:@"p2pConn == 0 !"];
        return;
    }
    
    if(_textFieldListenPort.text.length == 0){
        [self addStateText:@"port is empty!"];
        return;
    }
    int port = [_textFieldListenPort.text intValue];
    if(port <= 0 || port >65535){
        [self addStateText:@"invalid listen port!"];
        return;
    }
    
    int status = p2p_create_tcp_proxy(p2pTransport, p2pConn, port, &p2pLocalListenPort);
    
    NSString *string = [NSString stringWithFormat:@"p2p_create_tcp_proxy return %d, listen port %d", status, p2pLocalListenPort];
    [self addStateText:string];
}

- (IBAction)DestroyProxy:(id)sender {
    if( p2pTransport==0 ){
        [self addStateText:@"p2pTransport is 0 !"];
        return;
    }
    if( p2pConn == 0 ){
        [self addStateText:@"p2pConn == 0 !"];
        return;
    }
    
    if( p2pLocalListenPort == 0 ){
        [self addStateText:@"p2pLocalListenPort == 0 !"];
        return;
    }
    
    p2p_destroy_tcp_proxy(p2pTransport, p2pConn, p2pLocalListenPort);
    [self addStateText:@"tcp proxy destroyed"];
}

- (IBAction)SendFile:(id)sender {
    if( p2pTransport==0 ){
        [self addStateText:@"p2pTransport is 0 !"];
        return;
    }
    if( p2pConn == 0 ){
        [self addStateText:@"p2pConn == 0 !"];
        return;
    }
    
    [self performSelectorInBackground:@selector(p2pSendFile) withObject:nil];
}


-(void)p2pSendFile{
    FILE *f;
    char buffer[40960];
    size_t readed;
    int error;
    
    open_debug_file("p2psend", &f, "rb");
    if(!f)
        return;
    
    while( !feof(f) )
    {
        readed = fread(buffer, sizeof(char), sizeof(buffer), f);
        int sended = p2p_transport_send(p2pTransport, p2pConn, buffer, (int)readed, P2P_SEND_BLOCK, &error);
        if(sended <= 0)
        {
            break;
        }
    }
    
    fclose(f);
}

- (IBAction)requestDispatch:(id)sender{
    if(![self checkInput])
        return;
    
    if(_textFieldUser.text.length == 0){
        [self addStateText:@"user is empty!"];
        return ;
    }
    if(_textFieldPassword.text.length == 0){
        [self addStateText:@"password is empty!"];
        return ;
    }
    
    char server[256];
    char* user = (char*)[_textFieldUser.text UTF8String];
    char* password = (char*)[_textFieldPassword.text UTF8String];
    void* user_data = (__bridge void *)(self);
    
    sprintf(server, "%s:%d", [_textFieldServer.text UTF8String], [_textFieldDSPort.text intValue]);
    void* dispatcher = 0;
    p2p_request_dispatch_server(user, password, server, user_data, on_dispatch_result, &dispatcher);
}

- (IBAction)queryDispatch:(id)sender{
    if(![self checkInput])
        return;
    if(_textFieldRemoteUser.text.length == 0){
        [self addStateText:@"remote user is empty!"];
        return;
    }
    
    char server[256];
    char* remote_user = (char*)[_textFieldRemoteUser.text UTF8String];
    void* user_data = (__bridge void *)(self);
    
    sprintf(server, "%s:%d", [_textFieldServer.text UTF8String], [_textFieldDSPort.text intValue]);
    void* dispatcher = 0;
    p2p_query_dispatch_server(remote_user, server, user_data, on_dispatch_result, &dispatcher);
}

-(void)onCreateComplete{
    if(p2pStatus != P2P_SUCCESS){
        p2p_transport_destroy(p2pTransport);
        p2pTransport = 0;
    }
    NSString *string = [NSString stringWithFormat:@"onCreateComplete %d", p2pStatus];
    [self addStateText:string];
}

-(void)onConnectComplete{
    if(p2pStatus != P2P_SUCCESS){
        p2pConn = 0;
    }
    NSString *string = [NSString stringWithFormat:@"onConnectComplete %d", p2pStatus];
    [self addStateText:string];
}

-(void)onAcceptConnection{
    [self addStateText:@"accept remote user"];
}

-(void)onDisconnectConnection{
    p2pConn = 0;
    [self addStateText:@"remote user connection is disconnect"];
}
-(void)onDispatchResult
{
    NSString *string;
    if(p2pStatus == P2P_SUCCESS)
        string = [NSString stringWithFormat:@"onDispatchResult %@ %d %d", ds_result_server, ds_result_port, ds_result_svr_id];
    else
        string = [NSString stringWithFormat:@"onDispatchResult %d", p2pStatus];
    [self addStateText:string];
}

- (void)addStateText:(NSString*) text{
    NSString* str = [_textViewMsg.text stringByAppendingString:text];
    _textViewMsg.text = [str stringByAppendingString:@"\r\n"];
}
@end
