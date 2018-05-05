/*
Navicat MySQL Data Transfer

Source Server         : 127.0.0.1
Source Server Version : 50711
Source Host           : localhost:3306
Source Database       : remoteapp

Target Server Type    : MYSQL
Target Server Version : 50711
File Encoding         : 65001

Date: 2017-10-11 17:16:59
*/

SET FOREIGN_KEY_CHECKS=0;

-- ----------------------------
-- Table structure for dual
-- ----------------------------
DROP TABLE IF EXISTS `dual`;
CREATE TABLE `dual` (
  `dummy` varchar(100) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of dual
-- ----------------------------

-- ----------------------------
-- Table structure for tsr_assignreal
-- ----------------------------
DROP TABLE IF EXISTS `tsr_assignreal`;
CREATE TABLE `tsr_assignreal` (
  `id` int(11) NOT NULL,
  `createtime` datetime DEFAULT NULL,
  `uhpackage` varchar(20) DEFAULT NULL,
  `onlinep2p` varchar(20480) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


-- ----------------------------
-- Table structure for tsr_hwarereal
-- ----------------------------
DROP TABLE IF EXISTS `tsr_hwarereal`;
CREATE TABLE `tsr_hwarereal` (
  `id` int(11) NOT NULL,
  `cpupct` varchar(20) DEFAULT NULL,
  `ramsize` varchar(20) DEFAULT NULL,
  `getflow` varchar(20) DEFAULT NULL,
  `sendflow` varchar(20) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tsr_hwarereal
-- ----------------------------

-- ----------------------------
-- Table structure for tsr_p2preal
-- ----------------------------
DROP TABLE IF EXISTS `tsr_p2preal`;
CREATE TABLE `tsr_p2preal` (
  `id` int(11) NOT NULL,
  `bcapacity` varchar(20) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Table structure for tx_bdrule
-- ----------------------------
DROP TABLE IF EXISTS `tx_bdrule`;
CREATE TABLE `tx_bdrule` (
  `BDCODE` varchar(10) NOT NULL DEFAULT '',
  `TABLENAME` varchar(50) DEFAULT NULL,
  `TABLEONAME` varchar(250) DEFAULT NULL,
  `PKCOLNAME` varchar(20) DEFAULT NULL,
  `QUERYSQL` varchar(2000) DEFAULT NULL,
  `EDITSQL` varchar(2000) DEFAULT NULL,
  `GRIDCOLNAME` varchar(500) DEFAULT NULL,
  `GRIDCAPTION` varchar(500) DEFAULT NULL,
  `GRIDCOLWIDTH` varchar(200) DEFAULT NULL,
  `QPARATYPE` varchar(150) DEFAULT NULL,
  `VERIFYCOLS` varchar(100) DEFAULT NULL,
  `GRIDCOLALIGN` varchar(400) DEFAULT NULL,
  `DDLQUERYSQL` varchar(600) DEFAULT NULL,
  `TREEQUERYSQL` varchar(600) DEFAULT NULL,
  `PIDCOLNAME` varchar(20) DEFAULT NULL,
  `JSIDNAME` varchar(50) DEFAULT NULL,
  `BMIDNAME` varchar(50) DEFAULT NULL,
  `ORDERCOLS` varchar(100) DEFAULT NULL,
  `QX_COL` varchar(50) DEFAULT NULL,
  `FTEXTCOL` varchar(100) DEFAULT NULL,
  `ZZ_STATESQL` varchar(200) DEFAULT NULL,
  `SH_STATESQL` varchar(200) DEFAULT NULL,
  `CX_STATESQL` varchar(200) DEFAULT NULL,
  `LINK_SQL` varchar(1000) DEFAULT NULL,
  `LINK_PARA` varchar(1000) DEFAULT NULL,
  `CHECK_USE` varchar(1000) DEFAULT NULL,
  PRIMARY KEY (`BDCODE`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_bdrule
-- ----------------------------
INSERT INTO `tx_bdrule` VALUES ('10001', 'tx_restype', null, 'restypeid', 'select restypeid,name,code,date_format(createtime,\'%Y-%m-%d %T\') createtime,creator,modifier,date_format(modifytime ,\'%Y-%m-%d %T\') modifytime from tx_restype a where 1=1', 'select restypeid,name,code from tx_restype where restypeid=?', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('10002', 'tx_gvlist', null, 'gvlistid', 'select gvlistid,restypeid,name,code,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime,creator,modifier,date_format(modifytime ,\'%Y-%m-%d %H:%m:%s\') modifytime from tx_gvlist a where 1=1', 'select gvlistid,restypeid,name,code,createtime,creator from tx_gvlist where gvlistid=?', null, null, null, 'E_restypeid', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('10003', 'tx_syslog', null, 'exceptionid', 'select exceptionid,userid user,systemid,orgid,exceptiontype,date_format(exceptiontime,\'%Y-%m-%d %T\') exceptiontime,logpath,oprMemo from tx_syslog a where 1=1 ', null, null, null, null, null, null, null, null, null, null, null, null, 'order by exceptiontime desc', null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('20001', 'tx_button', null, 'buttonid', 'select buttonid,buttonname,function,shortcut,isenabled,creator,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime,modifier,date_format(modifytime,\'%Y-%m-%d %H:%m:%s\') modifytime,orgid from tx_button ', 'select buttonid,buttonname,function,shortcut,isenabled,creator,createtime,modifier,modifytime,orgid from tx_button where buttonid=?', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('20002', 'tx_org', null, 'oid', 'select oid,name,orgtype,foid,isenabled,creator,date_format(createtime ,\'%Y-%m-%d %H:%m:%s\') createtime,modifier,date_format(modifytime ,\'%Y-%m-%d %H:%m:%s\') modifytime,orgid,remark from tx_org where isenabled=1', 'select oid,name,orgtype,foid,isenabled,creator,createtime,modifier,modifytime,orgid,remark from tx_org where isenabled=1 and oid=?', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('20003', 'tx_employee', null, 'employeeid', 'select a.employeeid,a.code,a.name,a.deptid,a.mobilephone,a.officephone,a.familyphone,case a.sex when \'0\' then \'女\' when \'1\' then \'男\' end sex,a.educdegree,a.birthday,a.ismarried,a.certtype,a.certno,a.isenabled,a.creator,date_format(a.createtime ,\'%Y-%m-%d %H:%m:%s\') createtime,a.modifier,date_format(a.modifytime ,\'%Y-%m-%d %H:%m:%s\') modifytime,a.orgid,b.name deptname from tx_employee a,tx_org b where a.isenabled=1 and b.isenabled=1 and a.deptid=b.oid', 'select employeeid,code,name,deptid,mobilephone,officephone,familyphone,sex,educdegree,date_format(birthday,\'%Y-%m-%d\') birthday,ismarried,certtype,certno,isenabled,creator,createtime,modifier,modifytime,orgid from tx_employee where isenabled=1 and employeeid=?', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('30001', 'tx_device', null, 'devid', 'select devid,guid,setupdate,setupno,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime,custname,curserver,curserverport,curserverid,position from tx_device where 1=1', 'select devid,guid,position,setupno from tx_device where devid=?', 'guid,position,setupno', 'GUID,位置,密码', null, '', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('30002', 'tx_server', null, 'serverid', 'select serverid,servername,(case when servertype=\'1\' then \'分派服务器\' when servertype=\'2\' then \'p2p服务器\' end) servertype,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime from tx_server where 1=1', 'select serverid,uid,servername,servertype where serverid=?', 'servername,servertype', '服务器名称,服务器类型', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('30003', 'tx_hwareserver', null, 'hwid', 'select hwid,servername,ramsize,cpu,position,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime from tx_hwareserver where 1=1', null, 'servername,ramsize,cpu,position', '服务器名称,内存大小,CPU,位置', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);
INSERT INTO `tx_bdrule` VALUES ('90001', 'tx_user', null, 'userid', 'select userid,code,name,lasttime,loginnum,creator,date_format(createtime,\'%Y-%m-%d %H:%m:%s\') createtime,modifier,date_format(modifytime,\'%Y-%m-%d %H:%m:%s\') modifytime from tx_user', null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null);

-- ----------------------------
-- Table structure for tx_device
-- ----------------------------
DROP TABLE IF EXISTS `tx_device`;
CREATE TABLE `tx_device` (
  `DevID` int(11) NOT NULL AUTO_INCREMENT COMMENT '唯一键值',
  `GUID` varchar(128) NOT NULL COMMENT 'GUID,明文存放',
  `SetupDate` datetime DEFAULT NULL,
  `SetupNo` varchar(50) DEFAULT NULL,
  `CurServer` varchar(128) DEFAULT NULL,
  `CurServerPort` int(11) DEFAULT NULL,
  `CurServerId` int(11) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL,
  `custname` varchar(50) DEFAULT NULL,
  `position` varchar(200) DEFAULT NULL,
  `GssServer` varchar(128) DEFAULT NULL,
  `GssServerPort` int(11) DEFAULT NULL,
  `GssServerId` int(11) DEFAULT NULL,
  PRIMARY KEY (`DevID`,`GUID`),
  KEY `INX_GUID` (`GUID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_device
-- ----------------------------

-- ----------------------------
-- Table structure for tx_device_server
-- ----------------------------
DROP TABLE IF EXISTS `tx_device_server`;
CREATE TABLE `tx_device_server` (
  `deviceid` int(18) NOT NULL,
  `serverid` int(18) DEFAULT NULL,
  PRIMARY KEY (`deviceid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_device_server
-- ----------------------------

-- ----------------------------
-- Table structure for tx_hwareserver
-- ----------------------------
DROP TABLE IF EXISTS `tx_hwareserver`;
CREATE TABLE `tx_hwareserver` (
  `hwid` int(11) NOT NULL,
  `servername` varchar(100) DEFAULT NULL,
  `ramsize` varchar(200) DEFAULT NULL,
  `cpu` varchar(200) DEFAULT NULL,
  `position` varchar(200) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL,
  `remark` varchar(1000) DEFAULT NULL,
  PRIMARY KEY (`hwid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_hwareserver
-- ----------------------------

-- ----------------------------
-- Table structure for tx_iemodel
-- ----------------------------
DROP TABLE IF EXISTS `tx_iemodel`;
CREATE TABLE `tx_iemodel` (
  `IECode` varchar(20) NOT NULL,
  `Name` varchar(100) DEFAULT NULL,
  `Code` varchar(100) DEFAULT NULL,
  `PKColName` varchar(20) DEFAULT NULL,
  `SQName` varchar(50) DEFAULT NULL,
  `CHSQL` varchar(2000) DEFAULT NULL,
  `Path` varchar(100) DEFAULT NULL,
  `Remark` varchar(500) DEFAULT NULL,
  `OrderNo` int(11) NOT NULL DEFAULT '1',
  `IsEnabled` varchar(1) NOT NULL DEFAULT '1',
  `IsSystem` varchar(1) NOT NULL DEFAULT '0',
  `Creator` varchar(50) DEFAULT NULL,
  `CreateTime` datetime DEFAULT NULL,
  `Modifier` varchar(50) DEFAULT NULL,
  `ModifyTime` datetime DEFAULT NULL,
  `ORGID` int(11) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_iemodel
-- ----------------------------
INSERT INTO `tx_iemodel` VALUES ('100004', '出国访问', 'tJW_CGFW', 'CGFWID', 'sJW_CGFW', null, '\\importexcel\\modelfile\\出国访问.xls', 'OK he', '1', '1', '0', '系统管理员', '2010-12-07 21:32:15', '系统管理员', '2010-12-20 17:54:41', null);
INSERT INTO `tx_iemodel` VALUES ('100005', '课程', 'tJW_KC', 'ID', 'sJW_KC', null, '\\importexcel\\modelfile\\课程.xls', '暂不用', '1', '1', '0', '系统管理员', '2010-12-08 14:56:23', '系统管理员', '2011-12-19 20:47:51', null);
INSERT INTO `tx_iemodel` VALUES ('100006', '班级', 'tJW_BJ', 'BJID', 'sJW_BJ', null, '\\importexcel\\modelfile\\班级.xls', 'OK', '1', '1', '0', '系统管理员', '2010-12-08 15:24:44', '系统管理员', '2010-12-22 14:36:14', null);
INSERT INTO `tx_iemodel` VALUES ('100009', '承担项目', 'tJW_CDXM', 'CDXMID', 'sJW_CDXM', null, '\\importexcel\\modelfile\\承担项目.xls', 'OK', '1', '1', '0', '系统管理员', '2010-12-08 15:35:51', '系统管理员', '2010-12-09 12:40:47', null);
INSERT INTO `tx_iemodel` VALUES ('100010', '参加会议', 'tJW_CJHY', 'CJHYID', 'sJW_CJHY', null, '\\importexcel\\modelfile\\参加会议.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 15:40:35', '系统管理员', '2011-03-04 17:07:37', null);
INSERT INTO `tx_iemodel` VALUES ('100011', '服务活动', 'tJW_FWHD', 'FWHDID', 'sJW_FWHD', null, '\\importexcel\\modelfile\\服务活动.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 15:44:01', '系统管理员', '2010-12-09 15:26:58', null);
INSERT INTO `tx_iemodel` VALUES ('100012', '获奖情况', 'tJW_HJQK', 'HJQKID', 'sJW_HJQK', null, '\\importexcel\\modelfile\\获奖情况.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 15:46:32', '系统管理员', '2010-12-09 16:06:45', null);
INSERT INTO `tx_iemodel` VALUES ('100013', '合作院校', 'tJW_HZYX', 'HZYXID', 'sJW_HZYX', null, '\\importexcel\\modelfile\\合作院校.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 15:50:03', '系统管理员', '2011-01-13 17:51:50', null);
INSERT INTO `tx_iemodel` VALUES ('100014', '交流访问', 'tJW_JLFW', 'JLFWID', 'sJW_JLFW', null, '\\importexcel\\modelfile\\交流访问.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 15:56:33', '系统管理员', '2010-12-09 16:40:03', null);
INSERT INTO `tx_iemodel` VALUES ('100015', '优良课程', 'tJW_JPKC', 'JPKCID', 'sJW_JPKC', null, '\\importexcel\\modelfile\\优良课程.xls', '暂不用  ok he 限定教师列表和学历列表暂未实现', '1', '1', '0', '系统管理员', '2010-12-08 15:58:56', '系统管理员', '2010-12-24 11:25:02', null);
INSERT INTO `tx_iemodel` VALUES ('100017', '指导学生清单', 'tJW_JSZDSSQD', 'JSZDSSQDID', 'sJW_JSZDSSQD', null, '\\importexcel\\modelfile\\指导学生清单.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:03:12', '系统管理员', '2012-07-09 14:08:50', null);
INSERT INTO `tx_iemodel` VALUES ('100018', '教务活动', 'tJW_JWHD', 'JWHDID', 'sJW_JWHD', null, '\\importexcel\\modelfile\\教务活动.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:05:08', '系统管理员', '2012-07-06 08:09:33', null);
INSERT INTO `tx_iemodel` VALUES ('100019', '课程表', 'tJW_KCB', 'KCBID', 'sJW_KCB', null, '\\importexcel\\modelfile\\课程表.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:07:39', '系统管理员', '2011-12-16 11:31:28', null);
INSERT INTO `tx_iemodel` VALUES ('100020', '课程建设', 'tJW_KCJS', 'KCJSID', 'sJW_KCJS', null, '\\importexcel\\modelfile\\课程建设.xls', '暂不用', '1', '1', '0', '系统管理员', '2010-12-08 16:11:20', '系统管理员', '2010-12-09 17:28:52', null);
INSERT INTO `tx_iemodel` VALUES ('100023', '论文', 'tJW_KYLW', 'KYLWID', 'sJW_KYLW', null, '\\importexcel\\modelfile\\论文.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:19:50', '系统管理员', '2010-12-09 17:36:00', null);
INSERT INTO `tx_iemodel` VALUES ('100024', '评教项目', 'tJW_PJXM', 'ID', 'sJW_PJXM', null, '\\importexcel\\modelfile\\评教项目.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:40:47', '系统管理员', '2010-12-09 17:49:38', null);
INSERT INTO `tx_iemodel` VALUES ('100025', '培训班人员', 'tJW_PXBRY', 'PXBID', 'sJW_PXBRY', null, '\\importexcel\\modelfile\\培训班人员.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:41:41', '系统管理员', '2010-12-09 17:55:53', null);
INSERT INTO `tx_iemodel` VALUES ('100026', '事件记录', 'tJW_SJJL', 'SJJLID', 'sJW_SJJL', null, '\\importexcel\\modelfile\\事件记录.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:43:16', '系统管理员', '2010-12-09 17:52:27', null);
INSERT INTO `tx_iemodel` VALUES ('100027', '调停课清单', 'tJW_TTKQD', 'TTKID', 'sJW_TTKQD', null, '\\importexcel\\modelfile\\调停课清单.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:44:41', '系统管理员', '2010-12-20 13:39:59', null);
INSERT INTO `tx_iemodel` VALUES ('100028', '国际交流生', 'tJW_XJ_DWJLS', 'ID', 'sJW_XJ_DWJLS', null, '\\importexcel\\modelfile\\国际交流生.xls', null, '1', '1', '0', '系统管理员', '2010-12-08 16:46:06', '系统管理员', '2011-04-26 08:59:19', null);
INSERT INTO `tx_iemodel` VALUES ('100030', '学生成绩', 'tJW_XSCJ', 'XSCJID', 'sJW_XSCJ', null, '\\importexcel\\modelfile\\学生成绩.xls', 'ok', '1', '1', '0', '系统管理员', '2010-12-08 16:55:23', '系统管理员', '2010-12-24 16:54:37', null);
INSERT INTO `tx_iemodel` VALUES ('100031', '学生担任助教', 'tJW_XSDRZJ', 'XSDRZJID', 'sJW_XSDRZJ', null, '\\importexcel\\modelfile\\学生担任助教.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 16:58:31', '系统管理员', '2010-12-10 09:17:34', null);
INSERT INTO `tx_iemodel` VALUES ('100032', '学位点', 'tJW_XWD', 'XWDID', 'sJW_XWD', null, '\\importexcel\\modelfile\\学位点.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 17:00:30', '系统管理员', '2011-01-06 18:19:08', null);
INSERT INTO `tx_iemodel` VALUES ('100033', '参与项目登记', 'tJW_ZDXSKYXM', 'ZDXSKYXMID', 'sJW_ZDXSKYXM', null, '\\importexcel\\modelfile\\参与项目登记.xls', 'ok he 指导学生科研项目', '1', '1', '0', '系统管理员', '2010-12-08 17:02:36', '系统管理员', '2011-03-04 13:41:03', null);
INSERT INTO `tx_iemodel` VALUES ('100034', '专业', 'tJW_ZY', 'ZYID', 'sJW_ZY', null, '\\importexcel\\modelfile\\专业.xls', 'ok he', '1', '1', '0', '系统管理员', '2010-12-08 17:03:56', '系统管理员', '2011-01-13 10:42:10', null);
INSERT INTO `tx_iemodel` VALUES ('100035', '著作情况', 'tJW_ZZQK', 'ZZQKID', 'sJW_ZZQK', null, '\\importexcel\\modelfile\\著作情况.xls', 'ok', '1', '1', '0', '系统管理员', '2010-12-08 17:04:56', '系统管理员', '2010-12-24 16:05:28', null);
INSERT INTO `tx_iemodel` VALUES ('100036', '教师档案', 'tJW_JSDA', 'JSDAID', 'sJW_JSDA', null, '\\importexcel\\modelfile\\教师档案.xls', '只能导主要信息', '1', '1', '0', '系统管理员', '2010-12-13 14:17:31', '系统管理员', '2011-01-04 19:24:50', null);
INSERT INTO `tx_iemodel` VALUES ('100037', '学籍', 'tJW_XJ', 'XJID', 'sJW_XJ', null, '\\importexcel\\modelfile\\学籍.xls', null, '1', '1', '0', '系统管理员', '2010-12-13 14:47:59', '系统管理员', '2011-04-26 10:16:28', null);
INSERT INTO `tx_iemodel` VALUES ('100038', '课教评估', 'tJW_KJPG', 'KJPGID', 'sJW_KJPG', null, '\\importexcel\\modelfile\\课教评估.xls', null, '1', '1', '0', '系统管理员', '2010-12-13 19:38:56', '系统管理员', '2011-01-10 16:23:44', null);
INSERT INTO `tx_iemodel` VALUES ('100039', '责任编辑', 'tjw_zrbj', 'id', 'sjw_zrbj', null, '\\importexcel\\modelfile\\责任编辑.xls', null, '1', '1', '0', '系统管理员', '2010-12-31 10:26:53', '系统管理员', '2010-12-31 10:26:53', null);
INSERT INTO `tx_iemodel` VALUES ('100040', '科教评估项目', 'tJW_KJPG_XM', 'ID', 'sJW_KJPG_XM', null, null, null, '1', '1', '0', '系统管理员', '2011-01-10 17:00:48', '系统管理员', '2011-01-10 17:00:48', null);
INSERT INTO `tx_iemodel` VALUES ('100041', '教师工作量', 'tv_jsgzl', 'id', 'sv_jsgzl', null, '\\importexcel\\modelfile\\教师工作量.xls', null, '1', '1', '0', null, '2011-06-17 07:58:33', null, '2011-06-17 11:21:53', null);
INSERT INTO `tx_iemodel` VALUES ('100042', '供应商', 'tCG_GYS', 'gysid', 'sCG_GYS', null, '\\importexcel\\modelfile\\供应商.xls', null, '1', '1', '0', null, '2011-06-28 15:22:55', null, '2011-06-28 15:22:55', null);
INSERT INTO `tx_iemodel` VALUES ('100043', '教职工工资表', 'tJW_GZB', 'GZBID', 'sJW_GZB', null, '\\importexcel\\modelfile\\教职工工资表.xls', null, '1', '1', '0', '系统管理员', '2011-11-10 14:07:15', '系统管理员', '2011-11-10 15:58:51', null);
INSERT INTO `tx_iemodel` VALUES ('1001', '学科', 'tjw_xk', 'xkid', 'sjw_xk', null, '\\importexcel\\modelfile\\学科.xls', 'ok he', '1', '1', '0', 'init', '2010-11-29 00:00:00', '系统管理员', '2010-12-10 09:36:20', null);
INSERT INTO `tx_iemodel` VALUES ('200044', '教职工个税表', 'tJW_GSB', 'GSBID', 'sJW_GSB', null, '\\importexcel\\modelfile\\教职工个税表.xls', null, '1', '1', '0', '系统管理员', '2011-11-25 23:47:28', '系统管理员', '2012-11-23 14:59:43', null);
INSERT INTO `tx_iemodel` VALUES ('200045', '课程表时间', 'tJW_KCB_D', 'ID', 'sJW_KCB_D', null, '\\importexcel\\modelfile\\课程表时间.xls', null, '1', '1', '0', '系统管理员', '2011-12-18 20:03:47', '系统管理员', '2011-12-20 14:32:53', null);
INSERT INTO `tx_iemodel` VALUES ('210045', '日志主表', 'tJW_Note', 'NoteID', 'sJW_Note', null, '\\importexcel\\modelfile\\日志主表.xls', null, '1', '1', '0', '石涌岭', '2012-04-27 10:21:03', '石涌岭', '2012-04-27 10:21:03', null);
INSERT INTO `tx_iemodel` VALUES ('210046', '日志明细记录', 'tJW_Note_item', 'ID', 'sJW_Note_item', null, '\\importexcel\\modelfile\\日志明细记录.xls', null, '1', '1', '0', '石涌岭', '2012-04-27 10:24:21', '石涌岭', '2012-04-27 10:24:21', null);
INSERT INTO `tx_iemodel` VALUES ('210047', '工资收入', 'tCW_GZB', 'GZID', 'sCW_GZB', null, '\\importexcel\\modelfile\\工资收入.xls', null, '1', '1', '0', '系统管理员', '2012-05-18 16:21:00', '系统管理员', '2012-12-02 10:57:41', null);
INSERT INTO `tx_iemodel` VALUES ('210048', '福利津贴', 'tCW_JSFY', 'FYID', 'sCW_JSFY', null, '\\importexcel\\modelfile\\福利津贴.xls', null, '1', '1', '0', '系统管理员', '2012-05-18 20:30:58', '系统管理员', '2012-05-18 20:30:58', null);
INSERT INTO `tx_iemodel` VALUES ('210049', '设备资产管理', 'tgd_shebei', 'sbid', 'sgd_shebei', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21003‘  select dbo.decode(？,‘否,0,是,1,‘) from dual', '\\importexcel\\modelfile\\设备资产管理.xls', null, '1', '1', '0', '系统管理员', '2012-09-17 11:23:29', '系统管理员', '2012-09-17 17:35:07', null);
INSERT INTO `tx_iemodel` VALUES ('210050', '建筑类固定资产', 'tgd_jianzhu', 'jzid', 'sgd_jianzhu', null, '\\importexcel\\modelfile\\建筑类固定资产.xls', null, '1', '1', '0', '系统管理员', '2012-09-17 17:43:53', '系统管理员', '2012-09-17 17:50:14', null);
INSERT INTO `tx_iemodel` VALUES ('210051', '家具类固定资产', 'tgd_jiaju', 'jjid', 'sgd_jiaju', null, '\\importexcel\\modelfile\\家具类固定资产.xls', null, '1', '1', '0', '系统管理员', '2012-09-17 17:52:33', '系统管理员', '2012-09-18 09:56:20', null);
INSERT INTO `tx_iemodel` VALUES ('210052', '低值物品', 'tGD_DIZHI', 'dzid', 'sGD_DIZHI', null, '\\importexcel\\modelfile\\低值.xls', null, '1', '1', '0', '系统管理员', '2012-09-25 10:52:27', '系统管理员', '2013-06-23 21:41:14', null);
INSERT INTO `tx_iemodel` VALUES ('220045', '报名表', 'tjw_bmb', 'bmbid', 'sjw_bmb', null, '\\importexcel\\modelfile\\报名表.xls', null, '1', '1', '0', '张三', '2012-05-10 09:52:30', '张三', '2012-05-10 09:52:30', null);
INSERT INTO `tx_iemodel` VALUES ('220046', '论文答辩', 'tJW_LWDB', 'dbid', 'sJW_LWDB', null, '\\importexcel\\modelfile\\论文答辩.xls', null, '1', '1', '0', '系统管理员', '2012-05-10 15:24:06', '系统管理员', '2012-05-10 15:25:48', null);
INSERT INTO `tx_iemodel` VALUES ('220049', '应收学费明细', 'tJW_XF_MX', 'ID', 'sJW_XF_MX', null, null, null, '1', '1', '0', '系统管理员', '2012-10-24 00:07:07', '系统管理员', '2012-10-24 00:07:27', null);
INSERT INTO `tx_iemodel` VALUES ('220050', '导入薪酬计划', 'tJW_XCJH', 'ID', 'sJW_XCJH', null, '\\importexcel\\modelfile\\导入薪酬计划.xls', null, '1', '1', '0', '系统管理员', '2012-10-25 21:59:37', '系统管理员', '2012-10-25 21:59:37', null);
INSERT INTO `tx_iemodel` VALUES ('220051', '质量奖', 'tCW_ZLJ', 'ZLJID', 'sCW_ZLJ', null, '\\importexcel\\modelfile\\质量奖.xls', null, '1', '1', '0', '系统管理员', '2012-12-17 09:48:45', '系统管理员', '2014-09-18 09:17:23', null);
INSERT INTO `tx_iemodel` VALUES ('220052', '学生家长信息', 'tJW_XJ_JZ', 'JZID', 'sJW_XJ_JZ', null, '\\importexcel\\modelfile\\学生家长信息.xls', null, '1', '1', '0', '系统管理员', '2013-10-25 16:47:00', '系统管理员', '2013-10-25 16:48:24', null);
INSERT INTO `tx_iemodel` VALUES ('220053', '旅游经费结余', 'tCW_LYFJY', 'ID', 'sCW_LYFJY', null, '\\importexcel\\modelfile\\旅游经费结余.xls', null, '1', '1', '0', '系统管理员', '2013-11-29 14:44:38', '系统管理员', '2013-11-29 14:44:38', null);
INSERT INTO `tx_iemodel` VALUES ('220054', '应发实发', 'tJW_XMSZ', 'XMSZID', 'sJW_XMSZ', null, '\\importexcel\\modelfile\\应发实发.xls', null, '1', '1', '0', '系统管理员', '2014-01-16 14:30:26', '系统管理员', '2014-01-16 14:45:33', null);
INSERT INTO `tx_iemodel` VALUES ('220055', '福利劳务其它', 'tCW_FLQ', 'ID', 'sCW_FLQ', null, '\\importexcel\\modelfile\\福利劳务其它.xls', null, '1', '1', '0', '石涌岭', '2014-02-10 13:32:38', '系统管理员', '2014-02-11 15:20:46', null);
INSERT INTO `tx_iemodel` VALUES ('220056', '一次性课酬', 'tCW_YCXKC', 'YCXKCID', 'sCW_YCXKC', null, '\\importexcel\\modelfile\\一次性课酬.xls', null, '1', '1', '0', '系统管理员', '2014-03-25 09:17:35', '系统管理员', '2014-03-25 09:17:35', null);
INSERT INTO `tx_iemodel` VALUES ('220057', '学院托管经费', 'tCW_PJBXFL', 'ID', 'sCW_PJBXFL', null, '\\importexcel\\modelfile\\学院托管经费.xls', null, '1', '1', '0', '系统管理员', '2014-07-11 13:46:35', '系统管理员', '2014-07-11 13:46:35', null);
INSERT INTO `tx_iemodel` VALUES ('220058', '123', '123', '123', 's23', null, null, null, '1', '1', '0', '系统管理员', '2015-02-04 17:19:20', '系统管理员', '2015-02-04 17:21:24', null);
INSERT INTO `tx_iemodel` VALUES ('100001', '应用服务器', 'tx_server', 'serverid', 'tx_server', null, '\\importexcel\\modelfile\\应用服务器.xls', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `tx_iemodel` VALUES ('100002', '硬件服务器', 'tx_hwareserver', 'hwid', 'tx_hwareserver', null, '\\importexcel\\modelfile\\硬件服务器.xls', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `tx_iemodel` VALUES ('100003', '设备', 'tx_device', 'devid', 'tx_device', null, '/importexcel/modelfile/shebei.xls', null, '1', '1', '0', null, null, null, null, null);

-- ----------------------------
-- Table structure for tx_ie_col
-- ----------------------------
DROP TABLE IF EXISTS `tx_ie_col`;
CREATE TABLE `tx_ie_col` (
  `ECOLID` int(11) NOT NULL,
  `IECode` int(11) DEFAULT NULL,
  `Name` varchar(100) DEFAULT NULL,
  `Code` varchar(20) DEFAULT NULL,
  `iskeyCol` varchar(1) DEFAULT '0',
  `lhzj` varchar(1) DEFAULT NULL,
  `ISNULL` varchar(1) DEFAULT '1',
  `Len` int(11) DEFAULT NULL,
  `CType` varchar(10) DEFAULT '1',
  `GLSQL` varchar(1000) DEFAULT NULL,
  `Remark` varchar(100) DEFAULT NULL,
  `OrderNo` int(11) NOT NULL DEFAULT '1',
  `IsEnabled` varchar(1) NOT NULL DEFAULT '1',
  `IsSystem` varchar(1) NOT NULL DEFAULT '0',
  `Creator` varchar(50) DEFAULT NULL,
  `CreateTime` datetime DEFAULT NULL,
  `Modifier` varchar(50) DEFAULT NULL,
  `ModifyTime` datetime DEFAULT NULL,
  `ORGID` int(11) DEFAULT NULL,
  `ReferenceType` varchar(1) DEFAULT NULL,
  `isyw` varchar(20) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_ie_col
-- ----------------------------
INSERT INTO `tx_ie_col` VALUES ('101', '1001', '学科编号', 'XKBH', '0', '0', '0', '25', 'T', null, null, '1', '1', '0', 'init', '2010-11-29 00:00:00', '系统管理员', '2010-12-09 18:30:07', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210748', '100037', '教学管理部门', 'bmid', '0', '0', '0', '30', 'C', 'select orgid from tx_org where name = ？', null, '1', '1', '0', '系统管理员', '2012-05-30 15:38:24', '系统管理员', '2012-05-30 15:38:24', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100482', '100037', '学号', 'XSBH', '1', '0', '0', '20', 'T', null, null, '2', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2011-12-21 22:38:45', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100483', '100037', '姓名', 'XSMC', '0', '0', '0', '50', 'T', null, null, '3', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2011-01-13 18:06:21', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100484', '100037', '专业', 'ZYID', '0', '0', '1', '4', 'C', 'select ZYID from  tJW_ZY where ZYMC=？', null, '4', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2012-05-27 20:21:28', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100485', '100037', '性别', 'XB', '0', '0', '1', '2', 'C', 'select dbo.decode(？,‘女,0,男,1,‘) from dual', null, '5', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2011-01-10 14:20:05', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100486', '100037', '校区', 'XQID', '0', '0', '0', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21003‘', null, '6', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2012-05-27 20:21:28', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100488', '100037', '系所', 'XSID', '0', '0', '1', '4', 'C', 'select orgid from tx_org where name = ？', null, '8', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2012-05-30 15:42:36', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100514', '100037', '班级', 'BJID', '0', '0', '0', '4', 'C', 'select BJID from  tJW_BJ where BJMC=？', null, '9', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2012-05-27 20:21:28', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100490', '100037', '培养层次', 'PYCCID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘20017‘', null, '10', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2010-12-29 18:31:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100494', '100037', '学籍状态', 'XJZT', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21303‘', null, '14', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2010-12-13 17:02:58', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100495', '100037', '当前异动状态', 'DQYDZT', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21304‘', null, '15', '1', '0', '系统管理员', '2010-12-13 15:22:00', '系统管理员', '2010-12-13 17:02:58', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210702', '100037', '证件类型', 'zjlxid', '0', '0', '0', '20', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘20011‘', null, '16', '1', '0', '系统管理员', '2011-12-21 22:39:54', '系统管理员', '2011-12-21 22:40:45', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210703', '100037', '证件号码', 'zjhm', '0', '0', '0', '30', 'T', null, null, '17', '1', '0', '系统管理员', '2011-12-21 22:40:30', '系统管理员', '2011-12-21 22:40:30', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100499', '100037', '出生日期', 'CSRQ', '0', '0', '1', '8', 'D', null, null, '19', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:02:58', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100500', '100037', '出生地', 'CSD', '0', '0', '1', '25', 'T', null, null, '20', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:02:58', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100501', '100037', '政治面貌', 'ZZMM', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘20010‘', null, '21', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:36:04', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100503', '100037', '国家(地区)', 'GJID', '0', '0', '1', '4', 'C', 'select COUNTRYid from  TX_COUNTRY where name=？', null, '23', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-06 18:55:34', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100504', '100037', '民族', 'MZID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘20002‘', null, '24', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 18:04:38', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100513', '100037', '学生类别', 'XSLBID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21306‘', null, '33', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:36:04', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100515', '100037', '学制', 'XZID', '0', '0', '1', '10', 'T', null, null, '35', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:07:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100516', '100037', '入学方式', 'RXFSID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21310‘', null, '36', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:07:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100517', '100037', '入学年月', 'RXNY', '0', '0', '1', '8', 'D', null, null, '37', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:36:04', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100518', '100037', '毕业年月', 'BYNY', '0', '0', '1', '8', 'D', null, null, '38', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:36:04', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100519', '100037', '培养方式', 'PYFSID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21307‘', null, '39', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:46:13', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100520', '100037', '培养类型', 'PYLXID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21308‘', null, '40', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 15:24:46', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100521', '100037', '学习形式', 'XXXS', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21309‘', null, '41', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 18:04:38', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100525', '100037', '是否在职', 'SFZZ', '0', '0', '1', '2', 'T', 'select dbo.decode(？,‘否,0,是,1,’) from dual', null, '45', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-20 20:48:03', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100536', '100037', '实际毕业时间', 'SJBYSJ', '0', '0', '1', '8', 'D', null, null, '56', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:46:14', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100537', '100037', '在校时间', 'ZXSJ', '0', '0', '1', '8', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21917‘', null, '57', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:07:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100538', '100037', '毕业结果', 'BYJG', '0', '0', '1', '25', 'T', null, null, '58', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:46:14', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100552', '100037', '学生来源', 'XSLYID', '0', '0', '1', '4', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21311‘', null, '72', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-29 18:41:04', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100558', '100037', '入学前最终学历', 'RXQZZXL', '0', '0', '1', '50', 'C', 'select XLID from  tJW_XL where XLMC=？', null, '78', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-18 10:41:42', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100559', '100037', '入学前最终学位', 'RXQZZXW', '0', '0', '1', '25', 'T', null, null, '79', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-18 10:41:42', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100560', '100037', '入学前毕业学校', 'RXQBYXX', '0', '0', '1', '25', 'T', null, null, '80', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-18 10:41:42', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100562', '100037', '家庭住址', 'JTZZ', '0', '0', '1', '50', 'T', null, null, '82', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-29 18:31:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100563', '100037', '家庭住址邮编', 'JTZZYB', '0', '0', '1', '6', 'NT', null, null, '83', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-06 13:55:51', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100564', '100037', '家庭电话', 'JTDH', '0', '0', '1', '20', 'T', null, null, '84', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-13 17:56:10', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100565', '100037', '学校宿舍', 'XXSS', '0', '0', '1', '50', 'T', null, null, '85', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-06 14:38:24', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100566', '100037', '宿舍电话', 'SSDH', '0', '0', '1', '20', 'T', null, null, '86', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2010-12-29 18:31:52', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100567', '100037', '移动电话', 'YDDH', '0', '0', '1', '30', 'T', null, null, '87', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-06 15:09:33', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100568', '100037', '电子邮箱', 'DZYX', '0', '0', '1', '50', 'T', null, null, '88', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:38:17', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100569', '100037', 'QQ号', 'QQH', '0', '0', '1', '20', 'NT', null, null, '89', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:29:25', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100570', '100037', 'MSN号', 'MSNH', '0', '0', '1', '50', 'T', null, null, '90', '1', '0', '系统管理员', '2010-12-13 15:22:01', '系统管理员', '2011-01-13 14:29:25', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100652', '100037', '年级', 'NJ', '0', '0', '1', '20', 'T', null, null, '97', '1', '0', '系统管理员', '2011-01-06 21:52:07', '系统管理员', '2011-01-09 16:34:54', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100664', '100037', '培养类别', 'PYLBID', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21301‘', null, '104', '1', '0', '系统管理员', '2011-01-13 15:24:46', '系统管理员', '2011-01-13 16:10:15', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100665', '100037', '学校培养', 'pypylbid', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘21302‘', null, '105', '1', '0', '系统管理员', '2011-01-13 15:27:53', '系统管理员', '2012-05-27 20:18:51', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210744', '100037', '第二专业名称', 'dejy', '0', '0', '1', '50', 'T', null, null, '106', '1', '0', '系统管理员', '2012-05-27 20:15:39', '系统管理员', '2012-05-27 20:19:56', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210745', '100037', '本科大类班', 'bkdlbid', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘221001‘', null, '107', '1', '0', '系统管理员', '2012-05-27 20:18:51', '系统管理员', '2012-05-27 20:19:56', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210746', '100037', '本科模块课程班', 'bkmkbid', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘221003‘', null, '108', '1', '0', '系统管理员', '2012-05-27 20:19:56', '系统管理员', '2012-05-27 20:21:28', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('210747', '100037', '辅修情况', 'fxid', '0', '0', '1', '50', 'C', 'select gvlistid from tx_gvlist g where g.name=？ and g.restypeid=‘221002‘', null, '109', '1', '0', '系统管理员', '2012-05-27 20:21:28', '系统管理员', '2012-05-27 20:25:15', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221761', '100030', '归属类别', 'cjtype', '0', '0', '1', '0', 'C', 'select dbo.decode(？,‘研究生课程班,2,0‘)', null, '1', '1', '0', '系统管理员', '2012-10-26 03:10:23', '系统管理员', '2013-08-24 12:16:47', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221764', '100030', '学年', 'xn', '0', '0', '1', '0', 'NT', null, null, '2', '1', '0', '系统管理员', '2012-10-26 03:11:39', '系统管理员', '2013-08-24 12:16:47', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221759', '100030', '学期', 'xq', '0', '0', '1', '0', 'C', 'Select gvlistid from tx_gvlist where restypeid=21004 And name=？', null, '3', '1', '0', '系统管理员', '2012-10-26 03:09:57', '系统管理员', '2013-08-24 12:20:32', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221766', '100030', '课程表ID', 'kcbid', '0', '1', '0', '0', 'C', 'select kcbid from tjw_kcb where kcbid=？', null, '4', '1', '0', '系统管理员', '2012-10-26 03:12:01', '系统管理员', '2013-08-24 12:20:32', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('310097', '100030', '课程名称', 'kcmc', '0', '0', '1', '30', 'T', null, null, '5', '1', '0', '系统管理员', '2014-10-14 16:12:54', '系统管理员', '2014-10-14 16:12:54', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221765', '100030', '学号', 'xjid', '0', '1', '0', '0', 'C', 'select xjid from tjw_xj where xsbh=？', null, '6', '1', '0', '系统管理员', '2012-10-26 03:11:50', '系统管理员', '2014-10-14 16:12:26', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221760', '100030', '学生姓名', 'xsmc', '0', '0', '1', '0', '-', null, null, '7', '1', '0', '系统管理员', '2012-10-26 03:10:10', '系统管理员', '2014-10-14 16:12:26', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100328', '100030', '考试成绩', 'KSCJ', '0', '0', '0', '5', 'NT', null, null, '8', '1', '0', '系统管理员', '2010-12-08 16:57:02', '系统管理员', '2014-10-14 16:12:26', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100330', '100030', '考试结果', 'KSJG', '0', '0', '1', '25', 'T', null, null, '9', '1', '0', '系统管理员', '2010-12-08 16:57:02', '系统管理员', '2014-10-14 16:12:26', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('221763', '100030', '考试时间', 'kssj', '0', '0', '1', '0', 'D', null, null, '10', '1', '0', '系统管理员', '2012-10-26 03:11:08', '系统管理员', '2014-10-14 16:12:26', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('100329', '100030', '手机号码', 'yddh', '0', '0', '1', '50', 'T', null, null, '11', '1', '0', '系统管理员', '2010-12-08 16:57:02', '系统管理员', '2014-10-14 16:12:54', null, null, null);
INSERT INTO `tx_ie_col` VALUES ('3', '100001', '服务器名称', 'servername', '1', '0', '1', '100', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('4', '100001', '服务器类型', 'servertype', '0', '0', '1', '10', 'C', 'select (case when ?=\'分派服务器\' then \'1\' when ?=\'p2p服务器\' then \'2\' end)  from dual', null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('21', '100002', '服务器名称', 'servername', '0', '0', '1', '100', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('22', '100002', '内存大小', 'ramsize', '0', '0', '1', '20', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('23', '100002', 'CPU', 'cpu', '0', '0', '1', '20', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('24', '100002', '位置', 'position', '0', '0', '1', '200', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('41', '100003', 'GUID', 'guid', '1', '0', '1', '400', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('43', '100003', '位置', 'position', '0', '0', '1', '200', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);
INSERT INTO `tx_ie_col` VALUES ('42', '100003', '密码', 'setupno', '0', '0', '1', '200', 'T', null, null, '1', '1', '0', null, null, null, null, null, null, null);

-- ----------------------------
-- Table structure for tx_ie_msg
-- ----------------------------
DROP TABLE IF EXISTS `tx_ie_msg`;
CREATE TABLE `tx_ie_msg` (
  `ID` int(11) NOT NULL,
  `name` varchar(100) NOT NULL,
  `sj` datetime DEFAULT NULL,
  `lx` varchar(10) DEFAULT NULL,
  `emp` varchar(50) DEFAULT NULL,
  `num` int(11) DEFAULT NULL,
  `okids` varchar(8000) DEFAULT NULL,
  `tbname` varchar(50) DEFAULT NULL,
  `pkcol` varchar(50) DEFAULT NULL,
  `state` varchar(2) DEFAULT NULL,
  `Remark` varchar(100) DEFAULT NULL,
  `OrderNo` int(11) NOT NULL DEFAULT '1',
  `IsEnabled` varchar(1) NOT NULL DEFAULT '1',
  `IsSystem` varchar(1) NOT NULL DEFAULT '0',
  `Creator` varchar(50) DEFAULT NULL,
  `CreateTime` datetime DEFAULT NULL,
  `Modifier` varchar(50) DEFAULT NULL,
  `ModifyTime` datetime DEFAULT NULL,
  `ORGID` varchar(20) DEFAULT NULL,
  `ReferenceType` varchar(1) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_ie_msg
-- ----------------------------
INSERT INTO `tx_ie_msg` VALUES ('100005', '服务器', '2015-04-29 15:39:32', '新增保存', '系统管理员', '1', '100005', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 15:39:32', '系统管理员', '2015-04-29 15:39:32', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100006', '服务器', '2015-04-29 15:46:51', '新增保存', '系统管理员', '1', '100006', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 15:46:51', '系统管理员', '2015-04-29 15:46:51', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100007', '服务器', '2015-04-29 15:53:45', '新增保存', '系统管理员', '1', '100007', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 15:53:45', '系统管理员', '2015-04-29 15:53:45', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100008', '服务器', '2015-04-29 16:07:27', '新增保存', '系统管理员', '1', '100008', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 16:07:27', '系统管理员', '2015-04-29 16:07:27', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100009', '服务器', '2015-04-29 16:13:08', '新增保存', '系统管理员', '1', '100009', 'tx_server', 'serverid', '1', null, '1', '1', '0', '系统管理员', '2015-04-29 16:13:08', '系统管理员', '2015-04-29 16:13:08', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100010', '服务器', '2015-04-29 16:14:42', '新增保存', '系统管理员', '1', '100010', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 16:14:42', '系统管理员', '2015-04-29 16:14:42', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100011', '服务器', '2015-04-29 16:19:49', '新增保存', '系统管理员', '1', '100011', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 16:19:49', '系统管理员', '2015-04-29 16:19:49', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100012', '服务器', '2015-04-29 17:05:36', '新增保存', '系统管理员', '1', '100012', 'tx_server', 'serverid', '0', null, '1', '1', '0', '系统管理员', '2015-04-29 17:05:36', '系统管理员', '2015-04-29 17:05:36', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100014', '服务器', '2015-04-30 10:02:26', '新增保存', '', '1', '100014', 'tx_server', 'serverid', '0', null, '1', '1', '0', '', '2015-04-30 10:02:26', '', '2015-04-30 10:02:26', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100015', '服务器', '2015-04-30 10:11:11', '新增保存', '', '0', '', 'tx_server', 'serverid', '0', null, '1', '1', '0', '', '2015-04-30 10:11:11', '', '2015-04-30 10:11:11', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100016', '服务器', '2015-04-30 10:11:30', '覆盖保存', '', '1', '100006', 'tx_server', 'serverid', '1', null, '1', '1', '0', '', '2015-04-30 10:11:30', '', '2015-04-30 10:11:30', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100017', '服务器', '2015-04-30 11:22:00', '覆盖保存', '', '1', '100006', 'tx_server', 'serverid', '1', null, '1', '1', '0', '', '2015-04-30 11:22:00', '', '2015-04-30 11:22:00', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100018', '服务器', '2015-05-04 11:03:39', '新增保存', '', '1', '100018', 'tx_server', 'serverid', '1', null, '1', '1', '0', '', '2015-05-04 11:03:39', '', '2015-05-04 11:03:39', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100020', '硬件服务器', '2015-05-04 11:19:33', '新增保存', '', '1', '100002', 'tx_hwareserver', 'hwid', '1', null, '1', '1', '0', '', '2015-05-04 11:19:33', '', '2015-05-04 11:19:33', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100021', '设备', '2015-05-04 11:35:45', '新增保存', '', '1', '100001', 'tx_device', 'devid', '0', null, '1', '1', '0', '', '2015-05-04 11:35:45', '', '2015-05-04 11:35:45', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100022', '设备', '2015-05-21 16:08:16', '新增保存', '', '1', '100005', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-05-21 16:08:16', '', '2015-05-21 16:08:16', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100023', '设备', '2015-05-21 16:12:57', '新增保存', '', '1', '100007', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-05-21 16:12:57', '', '2015-05-21 16:12:57', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100024', '设备', '2015-06-16 18:54:34', '新增保存', '', '10', '100017,100018,100019,100020,100021,100022,100023,100024,100025,100026', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-06-16 18:54:34', '', '2015-06-16 18:54:34', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100025', '设备', '2015-06-17 17:10:11', '新增保存', '', '10', '100027,100028,100029,100030,100031,100032,100033,100034,100035,100036', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-06-17 17:10:11', '', '2015-06-17 17:10:11', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100028', '设备', '2015-08-07 11:08:37', '新增保存', '', '92', '102061,102062,102063,102064,102065,102066,102067,102068,102069,102070,102071,102072,102073,102074,102075,102076,102077,102078,102079,102080,102081,102082,102083,102084,102085,102086,102087,102088,102089,102090,102091,102092,102093,102094,102095,102096,102097,102098,102099,102100,102101,102102,102103,102104,102105,102106,102107,102108,102109,102110,102111,102112,102113,102114,102115,102116,102117,102118,102119,102120,102121,102122,102123,102124,102125,102126,102127,102128,102129,102130,102131,102132,102133,102134,102135,102136,102137,102138,102139,102140,102141,102142,102143,102144,102145,102146,102147,102148,102149,102150,102151,102152', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-08-07 11:08:37', '', '2015-08-07 11:08:37', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100030', '设备', '2015-08-28 18:16:48', '新增保存', '系统管理员', '1', '114071', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-08-28 18:16:48', '系统管理员', '2015-08-28 18:16:48', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100031', '设备', '2015-08-28 18:57:19', '覆盖保存', '系统管理员', '2', '114071,114071', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-08-28 18:57:19', '系统管理员', '2015-08-28 18:57:19', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100032', '设备', '2015-08-28 19:07:02', '覆盖保存', '', '2', '114071,114071', 'tx_device', 'devid', '1', null, '1', '1', '0', '', '2015-08-28 19:07:02', '', '2015-08-28 19:07:02', '', null);
INSERT INTO `tx_ie_msg` VALUES ('100033', '设备', '2015-08-29 10:04:39', '新增保存', '系统管理员', '0', '', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-08-29 10:04:39', '系统管理员', '2015-08-29 10:04:39', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100036', '设备', '2015-08-29 10:30:01', '新增保存', '系统管理员', '0', '', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-08-29 10:30:01', '系统管理员', '2015-08-29 10:30:01', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100037', '设备', '2015-08-29 10:30:52', '新增保存', '系统管理员', '0', '', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-08-29 10:30:52', '系统管理员', '2015-08-29 10:30:52', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100038', '设备', '2015-11-26 17:58:35', '新增保存', '系统管理员', '147', '114115,114116,114117,114118,114119,114120,114121,114122,114123,114124,114125,114126,114127,114128,114129,114130,114131,114132,114133,114134,114135,114136,114137,114138,114139,114140,114141,114142,114143,114144,114145,114146,114147,114148,114149,114150,114151,114152,114153,114154,114155,114156,114157,114158,114159,114160,114161,114162,114163,114164,114165,114166,114167,114168,114169,114170,114171,114172,114173,114174,114175,114176,114177,114178,114179,114180,114181,114182,114183,114184,114185,114186,114187,114188,114189,114190,114191,114192,114193,114194,114195,114196,114197,114198,114199,114200,114201,114202,114203,114204,114205,114206,114207,114208,114209,114210,114211,114212,114213,114214,114215,114216,114217,114218,114219,114220,114221,114222,114223,114224,114225,114226,114227,114228,114229,114230,114231,114232,114233,114234,114235,114236,114237,114238,114239,114240,114241,114242,114243,114244,114245,114246,114247,114248,114249,114250,114251,114252,114253,114254,114255,114256,114257,114258,114259,114260,114261', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2015-11-26 17:58:35', '系统管理员', '2015-11-26 17:58:35', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100039', '设备', '2016-04-07 11:40:23', '新增保存', '系统管理员', '89', '114262,114263,114264,114265,114266,114268,114270,114271,114273,114274,114275,114276,114277,114278,114279,114280,114281,114282,114283,114284,114285,114288,114289,114290,114291,114292,114293,114294,114295,114296,114298,114299,114300,114301,114302,114303,114304,114305,114306,114307,114308,114309,114310,114311,114312,114313,114314,114315,114317,114318,114319,114320,114321,114322,114323,114324,114325,114326,114327,114328,114329,114330,114331,114332,114333,114334,114335,114336,114337,114338,114339,114341,114342,114343,114344,114345,114346,114347,114348,114350,114351,114352,114355,114356,114357,114358,114359,114360,114361', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-04-07 11:40:23', '系统管理员', '2016-04-07 11:40:23', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100040', '设备', '2016-04-07 11:42:41', '覆盖保存', '系统管理员', '100', '114262,114263,114264,114265,114266,114218,114268,114204,114270,114271,114205,114273,114274,114275,114276,114277,114278,114279,114280,114281,114282,114283,114284,114285,114114,114190,114288,114289,114290,114291,114292,114293,114294,114295,114296,114174,114298,114299,114300,114301,114302,114303,114304,114305,114306,114307,114308,114309,114310,114311,114312,114313,114314,114315,114136,114317,114318,114319,114320,114321,114322,114323,114324,114325,114326,114327,114328,114329,114330,114331,114332,114333,114334,114335,114336,114337,114338,114339,114238,114341,114342,114343,114344,114345,114346,114347,114348,114193,114350,114351,114352,114209,114115,114355,114356,114357,114358,114359,114360,114361', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-04-07 11:42:41', '系统管理员', '2016-04-07 11:42:41', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100041', '设备', '2016-04-07 11:43:07', '覆盖保存', '系统管理员', '100', '114262,114263,114264,114265,114266,114218,114268,114204,114270,114271,114205,114273,114274,114275,114276,114277,114278,114279,114280,114281,114282,114283,114284,114285,114114,114190,114288,114289,114290,114291,114292,114293,114294,114295,114296,114174,114298,114299,114300,114301,114302,114303,114304,114305,114306,114307,114308,114309,114310,114311,114312,114313,114314,114315,114136,114317,114318,114319,114320,114321,114322,114323,114324,114325,114326,114327,114328,114329,114330,114331,114332,114333,114334,114335,114336,114337,114338,114339,114238,114341,114342,114343,114344,114345,114346,114347,114348,114193,114350,114351,114352,114209,114115,114355,114356,114357,114358,114359,114360,114361', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-04-07 11:43:07', '系统管理员', '2016-04-07 11:43:07', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100042', '设备', '2016-04-13 17:52:43', '新增保存', '系统管理员', '255', '114562,114563,114564,114565,114566,114567,114568,114569,114570,114571,114572,114573,114574,114575,114576,114577,114578,114579,114580,114581,114582,114583,114584,114585,114586,114587,114588,114589,114590,114591,114592,114593,114594,114595,114596,114597,114598,114599,114600,114601,114602,114603,114604,114605,114606,114607,114608,114609,114610,114611,114612,114613,114614,114615,114616,114617,114618,114619,114620,114621,114622,114623,114624,114625,114626,114627,114628,114629,114630,114631,114632,114633,114634,114635,114636,114637,114638,114639,114640,114641,114642,114643,114644,114645,114646,114647,114648,114649,114650,114651,114652,114653,114654,114655,114656,114657,114658,114659,114660,114661,114662,114663,114664,114665,114666,114667,114668,114669,114670,114671,114672,114673,114674,114675,114676,114677,114678,114679,114680,114681,114682,114683,114684,114685,114686,114687,114688,114689,114690,114691,114692,114693,114694,114695,114696,114697,114698,114699,114700,114701,114702,114703,114704,114705,114706,114707,114708,114709,114710,114711,114712,114713,114714,114715,114716,114717,114718,114719,114720,114721,114722,114723,114724,114725,114726,114727,114728,114729,114730,114731,114732,114733,114734,114735,114736,114737,114738,114739,114740,114741,114742,114743,114744,114745,114746,114747,114748,114749,114750,114751,114752,114753,114754,114755,114756,114757,114758,114759,114760,114761,114762,114763,114764,114765,114766,114767,114768,114769,114770,114771,114772,114773,114774,114775,114776,114777,114778,114779,114780,114781,114782,114783,114784,114785,114786,114787,114788,114789,114790,114791,114792,114793,114794,114795,114796,114797,114798,114799,114800,114801,114802,114803,114804,114805,114806,114807,114808,114809,114810,114811,114812,114813,114814,114815,114816', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-04-13 17:52:43', '系统管理员', '2016-04-13 17:52:43', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100043', '设备', '2016-06-27 11:46:34', '新增保存', '系统管理员', '50', '114818,114819,114820,114821,114822,114823,114824,114825,114826,114827,114828,114829,114830,114831,114832,114833,114834,114835,114836,114837,114838,114839,114840,114841,114842,114843,114844,114845,114846,114847,114848,114849,114850,114851,114852,114853,114854,114855,114856,114857,114858,114859,114860,114861,114862,114863,114864,114865,114866,114867', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-06-27 11:46:34', '系统管理员', '2016-06-27 11:46:34', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100044', '设备', '2016-08-16 16:48:53', '覆盖保存', '系统管理员', '200', '114870,114856,114872,114869,114874,114821,114876,114877,114878,114879,114863,114881,114882,114883,114884,114885,114886,114822,114888,114889,114890,114891,114833,114893,114894,114895,114896,114897,114898,114899,114900,114901,114902,114903,114904,114905,114906,114907,114908,114909,114910,114911,114912,114913,114914,114857,114826,114917,114918,114919,114920,114921,114922,114831,114834,114925,114835,114927,114928,114929,114850,114931,114932,114842,114934,114935,114838,114937,114938,114939,114940,114860,114942,114943,114944,114945,114840,114947,114851,114818,114950,114839,114952,114953,114954,114955,114956,114957,114958,114853,114827,114854,114962,114858,114964,114965,114966,114967,114968,114969,114970,114862,114972,114973,114974,114975,114976,114855,114828,114829,114980,114981,114982,114983,114823,114985,114986,114987,114988,114989,114819,114820,114992,114993,114994,114995,114861,114997,114824,114999,115000,114836,115002,115003,115004,115005,115006,115007,114841,115009,115010,115011,115012,114864,115014,114825,115016,115017,115018,115019,115020,115021,114837,115023,114868,115025,115026,115027,115028,115029,115030,114843,115032,114865,115034,115035,115036,115037,114832,115039,114852,115041,115042,115043,115044,115045,115046,115047,115048,115049,115050,115051,115052,114859,115054,115055,115056,115057,115058,115059,114830,115061,115062,115063,115064,115065,115066,115067,115068,115069', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-08-16 16:48:53', '系统管理员', '2016-08-16 16:48:53', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100045', '设备', '2016-08-16 17:00:01', '覆盖保存', '系统管理员', '300', '115070,115071,115072,115073,115074,115075,115076,115077,115078,115079,115080,115081,115082,115083,115084,115085,115086,115087,115088,115089,115090,115091,115092,115093,115094,115095,115096,115097,115098,115099,115100,115101,115102,115103,115104,115105,115106,115107,115108,115109,115110,115111,115112,115113,115114,115115,115116,115117,115118,115119,115120,115121,115122,115123,115124,115125,115126,115127,115128,115129,115130,115131,115132,115133,115134,115135,115136,115137,115138,115139,115140,115141,115142,115143,115144,115145,115146,115147,115148,115149,115150,115151,115152,115153,115154,115155,115156,115157,115158,115159,115160,115161,115162,115163,115164,115165,114603,115167,115168,115169,115170,115171,115172,115173,115174,115175,115176,115177,115178,115179,114604,115181,115182,115183,115184,115185,115186,115187,115188,115189,115190,115191,115192,115193,115194,115195,115196,115197,115198,115199,115200,114661,115202,115203,115204,115205,115206,115207,115208,115209,114676,114584,115212,115213,115214,115215,115216,115217,114707,115219,115220,115221,115222,115223,115224,115225,115226,115227,115228,114627,115230,115231,115232,115233,115234,115235,115236,115237,115238,115239,114645,115241,115242,115243,115244,115245,115246,115247,115248,115249,115250,114662,115252,115253,114628,115255,115256,115257,115258,115259,115260,115261,115262,115263,114646,115265,114587,115267,114607,115269,115270,115271,115272,115273,115274,115275,115276,115277,115278,115279,114589,115281,115282,115283,115284,114710,115286,115287,115288,115289,115290,115291,115292,114712,115294,115295,115296,115297,115298,115299,115300,115301,115302,115303,115304,115305,115306,115307,115308,115309,115310,115311,114692,115313,115314,115315,114629,115317,115318,115319,115320,115321,115322,115323,115324,115325,115326,115327,115328,115329,115330,115331,115332,115333,115334,115335,115336,115337,115338,115339,115340,115341,115342,115343,115344,114715,114716,115347,115348,115349,115350,115351,115352,115353,115354,115355,115356,115357,115358,114647,115360,115361,115362,115363,115364,115365,115366,115367,114630,115369', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-08-16 17:00:01', '系统管理员', '2016-08-16 17:00:01', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100046', '设备', '2016-08-16 17:36:45', '覆盖保存', '系统管理员', '300', '115370,115371,115372,115373,115374,115375,115376,115377,115378,115379,115380,115381,115382,115383,115384,115385,115386,115387,115388,115389,115390,115391,115392,115393,115394,115395,115396,115397,115398,115399,115400,115401,115402,115403,115404,115405,115406,115407,115408,115409,115410,115411,115412,115413,115414,115415,115416,115417,115418,115419,115420,115421,115422,115423,115424,115425,115426,115427,115428,115429,115430,115431,115432,115433,115434,115435,115436,115437,115438,115439,115440,115441,115442,115443,115444,115445,115446,115447,115448,115449,115450,115451,115452,115453,115454,115455,115456,115457,115458,115459,115460,115461,115462,115463,115464,115465,115466,115467,115468,115469,115470,115471,115472,115473,115474,115475,115476,115477,115478,115479,115480,115481,115482,115483,115484,115485,115486,115487,115488,115489,115490,115491,115492,115493,115494,115495,115496,115497,115498,115499,115500,115501,115502,115503,115504,115505,115506,115507,115508,115509,115510,115511,115512,115513,115514,115515,115516,115517,115518,115519,115520,115521,115522,115523,115524,115525,115526,115527,115528,115529,115530,115531,115532,115533,115534,115535,115536,115537,115538,115539,115540,115541,115542,115543,115544,115545,115546,115547,115548,115549,115550,114591,115552,115553,115554,115555,115556,114648,115558,115559,115560,115561,115562,115563,115564,115565,115566,115567,115568,115569,115570,115571,115572,115573,115574,114718,115576,114664,115578,115579,115580,115581,115582,115583,115584,115585,115586,115587,115588,115589,115590,115591,115592,115593,114679,115595,115596,115597,115598,115599,115600,115601,115602,115603,115604,115605,115606,115607,115608,115609,115610,115611,115612,115613,115614,115615,115616,115617,115618,115619,115620,115621,115622,115623,115624,115625,115626,115627,115628,114594,115630,115631,115632,115633,115634,115635,115636,115637,115638,114595,115640,115641,115642,115643,115644,115645,115646,115647,115648,115649,115650,115651,115652,115653,115654,115655,115656,115657,115658,115659,115660,115661,115662,115663,115664,115665,115666,115667,115668,115669', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-08-16 17:36:45', '系统管理员', '2016-08-16 17:36:45', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100047', '设备', '2016-08-31 16:04:19', '覆盖保存', '系统管理员', '300', '115370,115371,115372,115373,115374,115375,115376,115377,115378,115379,115380,115381,115382,115383,115384,115385,115386,115387,115388,115389,115390,115391,115392,115393,115394,115395,115396,115397,115398,115399,115400,115401,115402,115403,115404,115405,115406,115407,115408,115409,115410,115411,115412,115413,115414,115415,115416,115417,115418,115419,115420,115421,115422,115423,115424,115425,115426,115427,115428,115429,115430,115431,115432,115433,115434,115435,115436,115437,115438,115439,115440,115441,115442,115443,115444,115445,115446,115447,115448,115449,115450,115451,115452,115453,115454,115455,115456,115457,115458,115459,115460,115461,115462,115463,115464,115465,115466,115467,115468,115469,115470,115471,115472,115473,115474,115475,115476,115477,115478,115479,115480,115481,115482,115483,115484,115485,115486,115487,115488,115489,115490,115491,115492,115493,115494,115495,115496,115497,115498,115499,115500,115501,115502,115503,115504,115505,115506,115507,115508,115509,115510,115511,115512,115513,115514,115515,115516,115517,115518,115519,115520,115521,115522,115523,115524,115525,115526,115527,115528,115529,115530,115531,115532,115533,115534,115535,115536,115537,115538,115539,115540,115541,115542,115543,115544,115545,115546,115547,115548,115549,115550,114591,115552,115553,115554,115555,115556,114648,115558,115559,115560,115561,115562,115563,115564,115565,115566,115567,115568,115569,115570,115571,115572,115573,115574,114718,115576,114664,115578,115579,115580,115581,115582,115583,115584,115585,115586,115587,115588,115589,115590,115591,115592,115593,114679,115595,115596,115597,115598,115599,115600,115601,115602,115603,115604,115605,115606,115607,115608,115609,115610,115611,115612,115613,115614,115615,115616,115617,115618,115619,115620,115621,115622,115623,115624,115625,115626,115627,115628,114594,115630,115631,115632,115633,115634,115635,115636,115637,115638,114595,115640,115641,115642,115643,115644,115645,115646,115647,115648,115649,115650,115651,115652,115653,115654,115655,115656,115657,115658,115659,115660,115661,115662,115663,115664,115665,115666,115667,115668,115669', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-08-31 16:04:19', '系统管理员', '2016-08-31 16:04:19', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100048', '设备', '2016-08-31 16:05:40', '覆盖保存', '系统管理员', '300', '115370,115371,115372,115373,115374,115375,115376,115377,115378,115379,115380,115381,115382,115383,115384,115385,115386,115387,115388,115389,115390,115391,115392,115393,115394,115395,115396,115397,115398,115399,115400,115401,115402,115403,115404,115405,115406,115407,115408,115409,115410,115411,115412,115413,115414,115415,115416,115417,115418,115419,115420,115421,115422,115423,115424,115425,115426,115427,115428,115429,115430,115431,115432,115433,115434,115435,115436,115437,115438,115439,115440,115441,115442,115443,115444,115445,115446,115447,115448,115449,115450,115451,115452,115453,115454,115455,115456,115457,115458,115459,115460,115461,115462,115463,115464,115465,115466,115467,115468,115469,115470,115471,115472,115473,115474,115475,115476,115477,115478,115479,115480,115481,115482,115483,115484,115485,115486,115487,115488,115489,115490,115491,115492,115493,115494,115495,115496,115497,115498,115499,115500,115501,115502,115503,115504,115505,115506,115507,115508,115509,115510,115511,115512,115513,115514,115515,115516,115517,115518,115519,115520,115521,115522,115523,115524,115525,115526,115527,115528,115529,115530,115531,115532,115533,115534,115535,115536,115537,115538,115539,115540,115541,115542,115543,115544,115545,115546,115547,115548,115549,115550,114591,115552,115553,115554,115555,115556,114648,115558,115559,115560,115561,115562,115563,115564,115565,115566,115567,115568,115569,115570,115571,115572,115573,115574,114718,115576,114664,115578,115579,115580,115581,115582,115583,115584,115585,115586,115587,115588,115589,115590,115591,115592,115593,114679,115595,115596,115597,115598,115599,115600,115601,115602,115603,115604,115605,115606,115607,115608,115609,115610,115611,115612,115613,115614,115615,115616,115617,115618,115619,115620,115621,115622,115623,115624,115625,115626,115627,115628,114594,115630,115631,115632,115633,115634,115635,115636,115637,115638,114595,115640,115641,115642,115643,115644,115645,115646,115647,115648,115649,115650,115651,115652,115653,115654,115655,115656,115657,115658,115659,115660,115661,115662,115663,115664,115665,115666,115667,115668,115669', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-08-31 16:05:40', '系统管理员', '2016-08-31 16:05:40', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100051', '设备', '2016-09-08 13:49:02', '覆盖保存', '系统管理员', '147', '114581,114582,114583,114584,114585,114586,114587,114588,114589,114590,114591,114592,114593,114594,114595,114596,114597,114598,114599,114600,114601,114602,114603,114604,114605,114606,114607,114608,114609,114610,114611,114612,114613,114614,114615,114616,114617,114618,114619,114620,114621,114622,114623,114624,114625,114626,114627,114628,114629,114630,114631,114632,114633,114634,114635,114636,114637,114638,114639,114640,114641,114642,114643,114644,114645,114646,114647,114648,114649,114650,114651,114652,114653,114654,114655,114656,114657,114658,114659,114660,114661,114662,114663,114664,114665,114666,114667,114668,114669,114670,114671,114672,114673,114674,114675,114676,114677,114678,114679,114680,114681,114682,114683,114684,114685,114686,114687,114688,114689,114690,114691,114692,114693,114694,114695,114696,114697,114698,114699,114700,114701,114702,114703,114704,114705,114706,114707,114708,114709,114710,114711,114712,114713,114714,114715,114716,114717,114718,114719,114720,114721,114722,114723,114724,114725,114726,114727', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-08 13:49:02', '系统管理员', '2016-09-08 13:49:02', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100057', '设备', '2016-09-08 14:06:29', '覆盖保存', '系统管理员', '147', '114581,114582,114583,114584,114585,114586,114587,114588,114589,114590,114591,114592,114593,114594,114595,114596,114597,114598,114599,114600,114601,114602,114603,114604,114605,114606,114607,114608,114609,114610,114611,114612,114613,114614,114615,114616,114617,114618,114619,114620,114621,114622,114623,114624,114625,114626,114627,114628,114629,114630,114631,114632,114633,114634,114635,114636,114637,114638,114639,114640,114641,114642,114643,114644,114645,114646,114647,114648,114649,114650,114651,114652,114653,114654,114655,114656,114657,114658,114659,114660,114661,114662,114663,114664,114665,114666,114667,114668,114669,114670,114671,114672,114673,114674,114675,114676,114677,114678,114679,114680,114681,114682,114683,114684,114685,114686,114687,114688,114689,114690,114691,114692,114693,114694,114695,114696,114697,114698,114699,114700,114701,114702,114703,114704,114705,114706,114707,114708,114709,114710,114711,114712,114713,114714,114715,114716,114717,114718,114719,114720,114721,114722,114723,114724,114725,114726,114727', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-08 14:06:29', '系统管理员', '2016-09-08 14:06:29', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100060', '设备', '2016-09-08 14:12:35', '覆盖保存', '系统管理员', '2', '119273,119274', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-08 14:12:35', '系统管理员', '2016-09-08 14:12:35', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100061', '设备', '2016-09-08 14:15:40', '覆盖保存', '系统管理员', '101', '119273,119274,119277,119278,119279,119280,119281,119282,119283,119284,119285,119286,119287,119288,119289,119290,119291,119292,119293,119294,119295,119296,119297,119298,119299,119300,119301,119302,119303,119304,119305,119306,119307,119308,119309,119310,119311,119312,119313,119314,119315,119316,119317,119318,119319,119320,119321,119322,119323,119324,119325,119326,119327,119328,119329,119330,119331,119332,119333,119334,119335,119336,119337,119338,119339,119340,119341,119342,119343,119344,119345,119346,119347,119348,119349,119350,119351,119352,119353,119354,119355,119356,119357,119358,119359,119360,119361,119362,119363,119364,119365,119366,119367,119368,119369,119370,119371,119372,119373,119374,119375', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-08 14:15:40', '系统管理员', '2016-09-08 14:15:40', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100062', '设备', '2016-09-08 14:17:30', '覆盖保存', '系统管理员', '200', '119273,119274,119277,119278,119279,119280,119281,119282,119283,119284,119285,119286,119287,119288,119289,119290,119291,119292,119293,119294,119295,119296,119297,119298,119299,119300,119301,119302,119303,119304,119305,119306,119307,119308,119309,119310,119311,119312,119313,119314,119315,119316,119317,119318,119319,119320,119321,119322,119323,119324,119325,119326,119327,119328,119329,119330,119331,119332,119333,119334,119335,119336,119337,119338,119339,119340,119341,119342,119343,119344,119345,119346,119347,119348,119349,119350,119351,119352,119353,119354,119355,119356,119357,119358,119359,119360,119361,119362,119363,119364,119365,119366,119367,119368,119369,119370,119371,119372,119373,119374,119375,119477,119478,119479,119480,119481,119482,119483,119484,119485,119486,119487,119488,119489,119490,119491,119492,119493,119494,119495,119496,119497,119498,119499,119500,119501,119502,119503,119504,119505,119506,119507,119508,119509,119510,119511,119512,119513,119514,119515,119516,119517,119518,119519,119520,119521,119522,119523,119524,119525,119526,119527,119528,119529,119530,119531,119532,119533,119534,119535,119536,119537,119538,119539,119540,119541,119542,119543,119544,119545,119546,119547,119548,119549,119550,119551,119552,119553,119554,119555,119556,114847,114866,119559,119560,119561,119562,119563,119564,119565,119566,119567,114867,119569,119570,114845,119572,119573,119574,119575', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-08 14:17:30', '系统管理员', '2016-09-08 14:17:30', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100063', '设备', '2016-09-09 11:13:58', '覆盖保存', '系统管理员', '1', '114638', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-09-09 11:13:58', '系统管理员', '2016-09-09 11:13:58', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100064', '设备', '2016-11-30 14:23:58', '覆盖保存', '系统管理员', '300', '119577,119578,119579,119580,119581,119582,119583,119584,119585,119586,119587,119588,119589,119590,119591,119592,119593,119594,119595,119596,119597,119598,119599,119600,119601,119602,119603,119604,119605,119606,119607,119608,119609,119610,119611,119612,119613,119614,119615,119616,119617,119618,119619,119620,119621,119622,119623,119624,119625,119626,119627,119628,119629,119630,119631,119632,119633,119634,119635,119636,119637,119638,119639,119640,119641,119642,119643,119644,119645,119646,119647,119648,119649,119650,119651,119652,119653,119654,119655,119656,119657,119658,119659,119660,119661,119662,119663,119664,119665,119666,119667,119668,119669,119670,119671,119672,119673,119674,119675,119676,119677,119678,119679,119680,119681,119682,119683,119684,119685,119686,119687,119688,119689,119690,119691,119692,119693,119694,119695,119696,119697,119698,119699,119700,119701,119702,119703,119704,119705,119706,119707,119708,119709,119710,119711,119712,119713,119714,119715,119716,119717,119718,119719,119720,119721,119722,119723,119724,119725,119726,119727,119728,119729,119730,119731,119732,119733,119734,119735,119736,119737,119738,119739,119740,119741,119742,119743,119744,119745,119746,119747,119748,119749,119750,119751,119752,119753,119754,119755,119756,119757,119758,119759,119760,119761,119762,119763,119764,119765,119766,119767,119768,119769,119770,119771,119772,119773,119774,119775,119776,119777,119778,119779,119780,119781,119782,119783,119784,119785,119786,119787,119788,119789,119790,119791,119792,119793,119794,119795,119796,119797,119798,119799,119800,119801,119802,119803,119804,119805,119806,119807,119808,119809,119810,119811,119812,119813,119814,119815,119816,119817,119818,119819,119820,119821,119822,119823,119824,119825,119826,119827,119828,119829,119830,119831,119832,119833,119834,119835,119836,119837,119838,119839,119840,119841,119842,119843,119844,119845,119846,119847,119848,119849,119850,119851,119852,119853,119854,119855,119856,119857,119858,119859,119860,119861,119862,119863,119864,119865,119866,119867,119868,119869,119870,119871,119872,119873,119874,119875,119876', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-11-30 14:23:58', '系统管理员', '2016-11-30 14:23:58', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100065', '设备', '2016-11-30 14:32:56', '覆盖保存', '系统管理员', '300', '119578,119727,119579,119580,119581,119582,119583,119584,119585,119586,119587,119588,119589,119590,119591,119592,119593,119594,119595,119596,119597,119598,119599,119600,119601,119602,119603,119604,119605,119606,119607,119608,119609,119610,119611,119612,119613,119614,119615,119616,119617,119618,119619,119620,119621,119622,119623,119624,119625,119626,119627,119628,119629,119630,119631,119632,119633,119634,119635,119636,119637,119638,119639,119640,119641,119642,119643,119644,119645,119646,119647,119648,119649,119650,119651,119652,119653,119654,119655,119656,119657,119658,119659,119660,119661,119662,119663,119664,119665,119666,119667,119668,119669,119670,119671,119672,119673,119674,119675,119676,119677,119678,119679,119680,119681,119682,119683,119684,119685,119686,119687,119688,119689,119690,119691,119692,119693,119694,119695,119696,119697,119698,119699,119700,119701,119702,119703,119704,119705,119706,119707,119708,119709,119710,119711,119712,119713,119714,119715,119716,119717,119718,119719,119720,119721,119722,119723,119724,119725,119726,119577,119728,119729,119730,119731,119732,119733,119734,119735,119736,119737,119738,119739,119740,119741,119742,119743,119744,119745,119746,119747,119748,119749,119750,119751,119752,119753,119754,119755,119756,119757,119758,119759,119760,119761,119762,119763,119764,119765,119766,119767,119768,119769,119770,119771,119772,119773,119774,119775,119776,119777,119778,119779,119780,119781,119782,119783,119784,119785,119786,119787,119788,119789,119790,119791,119792,119793,119794,119795,119796,119797,119798,119799,119800,119801,119802,119803,119804,119805,119806,119807,119808,119809,119810,119811,119812,119813,119814,119815,119816,119817,119818,119819,119820,119821,119822,119823,119824,119825,119826,119827,119828,119829,119830,119831,119832,119833,119834,119835,119836,119837,119838,119839,119840,119841,119842,119843,119844,119845,119846,119847,119848,119849,119850,119851,119852,119853,119854,119855,119856,119857,119858,119859,119860,119861,119862,119863,119864,119865,119866,119867,119868,119869,119870,119871,119872,119873,119874,119875,119876', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2016-11-30 14:32:56', '系统管理员', '2016-11-30 14:32:56', '1001', null);
INSERT INTO `tx_ie_msg` VALUES ('100066', '设备', '2017-05-12 09:36:00', '新增保存', '系统管理员', '49', '120195,120196,120197,120198,120199,120200,120201,120202,120203,120204,120205,120206,120207,120208,120209,120210,120211,120212,120213,120214,120215,120216,120217,120218,120219,120220,120221,120222,120223,120224,120225,120226,120227,120228,120229,120230,120231,120232,120233,120234,120235,120236,120237,120238,120239,120240,120242,120243,120244', 'tx_device', 'devid', '1', null, '1', '1', '0', '系统管理员', '2017-05-12 09:36:00', '系统管理员', '2017-05-12 09:36:00', '1001', null);

-- ----------------------------
-- Table structure for tx_server
-- ----------------------------
DROP TABLE IF EXISTS `tx_server`;
CREATE TABLE `tx_server` (
  `ServerId` int(11) NOT NULL,
  `ServerName` varchar(100) DEFAULT NULL,
  `ServerType` varchar(2) DEFAULT '1',
  `CreateTime` datetime DEFAULT NULL,
  `remark` varchar(1000) DEFAULT NULL,
  PRIMARY KEY (`ServerId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_server
-- ----------------------------

-- ----------------------------
-- Table structure for tx_session
-- ----------------------------
DROP TABLE IF EXISTS `tx_session`;
CREATE TABLE `tx_session` (
  `DevGuid` varchar(128) DEFAULT NULL,
  `BeginTime` datetime DEFAULT NULL,
  `EndTime` datetime DEFAULT NULL,
  `Success` tinyint(4) DEFAULT NULL,
  `Type` tinyint(4) DEFAULT NULL,
  `ClientIP` varchar(32) DEFAULT NULL,
  `DevIP` varchar(32) DEFAULT NULL,
  `ClientGuid` varchar(128) DEFAULT NULL,
  `ClientSessionID` int(11) DEFAULT NULL,
  `SessionEndTime` datetime DEFAULT NULL,
  KEY `session` (`ClientGuid`,`ClientSessionID`) USING BTREE,
  KEY `time` (`BeginTime`,`EndTime`) USING BTREE,
  KEY `DevGuid` (`DevGuid`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

-- ----------------------------
-- Records of tx_session
-- ----------------------------

-- ----------------------------
-- Table structure for tx_syslog
-- ----------------------------
DROP TABLE IF EXISTS `tx_syslog`;
CREATE TABLE `tx_syslog` (
  `exceptionid` int(18) NOT NULL,
  `userid` varchar(100) DEFAULT NULL,
  `systemid` int(18) DEFAULT '0',
  `exceptiontype` varchar(200) DEFAULT NULL,
  `exceptiontime` datetime DEFAULT NULL,
  `logpath` varchar(200) DEFAULT NULL,
  `orgid` int(18) DEFAULT NULL,
  `oprMemo` varchar(400) DEFAULT NULL,
  PRIMARY KEY (`exceptionid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_syslog
-- ----------------------------

-- ----------------------------
-- Table structure for tx_user
-- ----------------------------
DROP TABLE IF EXISTS `tx_user`;
CREATE TABLE `tx_user` (
  `userid` int(18) NOT NULL,
  `name` varchar(50) NOT NULL,
  `code` varchar(50) NOT NULL,
  `pwd` varchar(32) NOT NULL,
  `lasttime` datetime DEFAULT NULL,
  `loginnum` int(18) NOT NULL DEFAULT '0',
  `mobileno` varchar(50) DEFAULT NULL,
  `email` varchar(100) DEFAULT NULL,
  `deptid` int(18) DEFAULT NULL,
  `remark` varchar(1000) DEFAULT NULL,
  `orderno` int(18) NOT NULL DEFAULT '1',
  `isenabled` int(18) NOT NULL DEFAULT '1',
  `issystem` int(18) NOT NULL DEFAULT '0',
  `creator` varchar(50) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL,
  `modifier` varchar(50) DEFAULT NULL,
  `modifytime` datetime DEFAULT NULL,
  `orgid` int(18) NOT NULL,
  PRIMARY KEY (`userid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of tx_user
-- ----------------------------
INSERT INTO `tx_user` VALUES ('1001', '系统管理员', 'admin', 'e10adc3949ba59abbe56e057f20f883e', '2017-06-15 19:03:05', '1097', null, null, null, null, '1', '1', '0', '系统管理员', '2015-01-04 10:28:53', '系统管理员', '2015-12-03 17:41:12', '1001');

-- ----------------------------
-- Table structure for t_seq
-- ----------------------------
DROP TABLE IF EXISTS `t_seq`;
CREATE TABLE `t_seq` (
  `seqid` varchar(100) NOT NULL,
  `currval` int(11) DEFAULT NULL,
  `remark` varchar(1000) DEFAULT NULL,
  `orderno` int(11) DEFAULT '1',
  `isenabled` varchar(1) DEFAULT '1',
  `issystem` varchar(1) DEFAULT '0',
  `creator` varchar(200) DEFAULT NULL,
  `createtime` datetime DEFAULT NULL,
  `modifier` varchar(50) DEFAULT NULL,
  `modifytime` datetime DEFAULT NULL,
  `orgid` int(11) DEFAULT NULL,
  PRIMARY KEY (`seqid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ----------------------------
-- Records of t_seq
-- ----------------------------
INSERT INTO `t_seq` VALUES ('SM_SERVERALARM', '100989', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_CMSERVER', '100004', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_CSERVER', '100007', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_DEVICE', '100022', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_DEVICEGROUP', '100010', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_DEVICEPLAY', '100025', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_DEVICETIME', '100014', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_PSERVER', '100005', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_RELOADCFG', '100004', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_SERVER', '100015', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_SSERVER', '100014', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SSM_THRESHOLD', '100004', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_BUTTON', '100027', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_EMPLOYEE', '100016', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_EMPLOYEE_CODE', '6', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_EMPLOYEE_CODES', '100004', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_GVLIST', '100023', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_IE_MSG', '100066', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_MENU', '100052', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_MENUBUTTON', '100098', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_ORG', '100022', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_RESTYPE', '100013', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_ROLE', '100006', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_ROLERIGHT', '100144', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_SYSLOG', '101279', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_USER', '100012', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('SX_USERROLE', '100022', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('TX_DEVICE', '120244', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('TX_HWARESERVER', '100015', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('TX_SERVER', '100034', null, '1', '1', '0', null, null, null, null, null);
INSERT INTO `t_seq` VALUES ('TX_USER', '100010', null, '1', '1', '0', null, null, null, null, null);

-- ----------------------------
-- Procedure structure for delete_data
-- ----------------------------
DROP PROCEDURE IF EXISTS `delete_data`;
DELIMITER ;;
CREATE DEFINER=`root`@`%` PROCEDURE `delete_data`()
BEGIN  
delete from tsr_assignreal where createtime < date_sub(curdate(),interval 31 day) ;
delete from tsr_hwarereal where createtime < date_sub(curdate(),interval 31 day) ;
delete from tsr_p2preal where createtime < date_sub(curdate(),interval 31 day) ;
delete from tx_session where BeginTime < date_sub(curdate(),interval 31 day) ;
delete from tx_syslog where exceptiontime < date_sub(curdate(),interval 31 day) ;
END
;;
DELIMITER ;

-- ----------------------------
-- Event structure for e_delete_data
-- ----------------------------
DROP EVENT IF EXISTS `e_delete_data`;
DELIMITER ;;
CREATE DEFINER=`root`@`%` EVENT `e_delete_data` ON SCHEDULE EVERY 1 DAY STARTS '2017-03-31 10:09:45' ON COMPLETION PRESERVE ENABLE DO call delete_data ()
;;
DELIMITER ;
