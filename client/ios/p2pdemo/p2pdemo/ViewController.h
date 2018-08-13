//
//  ViewController.h
//  p2pdemo
//
//  Created by yhzl on 15-3-17.
//  Copyright (c) 2015年 yhzl. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface ViewController : UIViewController

@property (strong, nonatomic) IBOutlet UITextField *textFieldServer;
@property (strong, nonatomic) IBOutlet UITextField *textFieldPort;
@property (strong, nonatomic) IBOutlet UITextField *textFieldUser;
@property (strong, nonatomic) IBOutlet UITextField *textFieldPassword;
@property (strong, nonatomic) IBOutlet UITextField *textFieldRemoteUser;
@property (strong, nonatomic) IBOutlet UITextField *textFieldListenPort;
@property (strong, nonatomic) IBOutlet UITextField *textFieldDSPort;

@property (strong, nonatomic) IBOutlet UITextView *textViewMsg;

- (IBAction)ConnectServer:(id)sender;
- (IBAction)DisconnectServer:(id)sender;
- (IBAction)ConnectUser:(id)sender;
- (IBAction)DisconnectUser:(id)sender;
- (IBAction)CreateProxy:(id)sender;
- (IBAction)DestroyProxy:(id)sender;
- (IBAction)SendFile:(id)sender;

- (IBAction)requestDispatch:(id)sender;
- (IBAction)queryDispatch:(id)sender;

-(void)onCreateComplete;
-(void)onConnectComplete;
-(void)onAcceptConnection;
-(void)onDisconnectConnection;
-(void)onDispatchResult;
@end

