#include<iostream>
#include<string>
using namespace std;
int main(){
    string s;
    cin>>s;
    while(true){
    int n=s.find(' ');
    if(n==s.npos) break;
     s.replace(n,1,"%20");
    }
    cout<<s;
}