drop database if exists Gomoku;
create database if not exists Gomoku;
use Gomoku;
create table if not exists user (
    id int unsigned primary key auto_increment,
    username varchar(32) unique key not null comment '用户名',
    password varchar(255) not null comment '密码',
    score int unsigned default 1000 comment '积分',
    total_count int unsigned default 0 comment '总场次',
    win_count int unsigned default 0 comment '胜利场次'
);
