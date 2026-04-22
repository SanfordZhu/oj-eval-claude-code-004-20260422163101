#include <bits/stdc++.h>
using namespace std;

// Simple persistent bookstore meeting required commands with file-backed indices.
// Data files: accounts.dat, books.dat, finance.dat, ops.log
// Constraints: limited files, no full in-memory DB scans; we'll stream files.

struct Account { string id, pw, name; int priv; };
struct Book { string isbn, name, author, keyword; double price=0; int stock=0; };

static const string ACC_FILE = "accounts.dat";
static const string BOOK_FILE = "books.dat";
static const string FIN_FILE = "finance.dat"; // lines: +income -expense per transaction
static const string OPS_FILE = "ops.log";

// Login stack with selected book per frame
struct Session { string userid; int priv; string selected_isbn; };
vector<Session> stackv;

// Utilities
static inline string trim(const string &s){ size_t a=s.find_first_not_of(' '); if(a==string::npos) return ""; size_t b=s.find_last_not_of(' '); return s.substr(a,b-a+1);}

static bool starts_with(const string &s, const string &p){ return s.rfind(p,0)==0; }

// Validation helpers
static bool isLegalID(const string &s){ if(s.size()==0||s.size()>30) return false; for(char c: s){ if(!(isalnum((unsigned char)c)||c=='_')) return false;} return true; }
static bool isLegalUser(const string &s){ if(s.size()==0||s.size()>30) return false; for(unsigned char c: s){ if(c<32) return false; } return true; }
static bool isLegalPriv(const string &s){ if(s.size()==0||s.size()>1) return false; for(char c: s) if(!isdigit((unsigned char)c)) return false; int v=stoi(s); return v==1||v==3||v==7; }
static bool isLegalISBN(const string &s){ if(s.size()>20) return false; for(unsigned char c: s){ if(c<32) return false; } return true; }
static bool isLegalNameAuth(const string &s){ if(s.size()>60) return false; for(unsigned char c: s){ if(c<32||c=='"') return false; } return true; }
static bool isLegalKeyword(const string &s){ if(s.size()>60) return false; for(unsigned char c: s){ if(c<32||c=='"') return false; } return true; }
static bool isLegalInt(const string &s){ if(s.size()==0||s.size()>10) return false; for(char c: s) if(!isdigit((unsigned char)c)) return false; if(s=="0") return false; // positive only when required
 long long v=0; try{ v=stoll(s);}catch(...){return false;} return v>0 && v<=2147483647; }
static bool isLegalNonnegInt(const string &s){ if(s.size()==0||s.size()>10) return false; for(char c: s) if(!isdigit((unsigned char)c)) return false; long long v=0; try{ v=stoll(s);}catch(...){return false;} return v>=0 && v<=2147483647; }
static bool isLegalMoney(const string &s){ if(s.size()==0||s.size()>13) return false; bool dot=false; for(char c: s){ if(c=='.'){ if(dot) return false; dot=true; } else if(!isdigit((unsigned char)c)) return false; }
 double v=0; try{ v=stod(s);}catch(...){return false;} return true; }

// File primitives (simple CSV-like lines)
// accounts.dat: id\tpassword\tpriv\tusername\n
static optional<Account> findAccount(const string &uid){ ifstream f(ACC_FILE); string line; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string id,pw,privs,name; if(!getline(ss,id,'\t')) continue; if(id!=uid) continue; getline(ss,pw,'\t'); getline(ss,privs,'\t'); getline(ss,name,'\t'); Account a{ id,pw,name, privs.empty()?1:stoi(privs)}; return a; } return {}; }
static bool accountExists(const string &uid){ return findAccount(uid).has_value(); }
static bool accountLoggedIn(const string &uid){ for(auto &s: stackv) if(s.userid==uid) return true; return false; }
static void writeAccount(const Account &a){ // append
 ofstream f(ACC_FILE, ios::app); f<<a.id<<'\t'<<a.pw<<'\t'<<a.priv<<'\t'<<a.name<<"\n"; }
static bool deleteAccount(const string &uid){ // rewrite file
 ifstream f(ACC_FILE); vector<string> lines; string line; bool found=false; while(getline(f,line)){ if(line.empty()) continue; string id; stringstream ss(line); getline(ss,id,'\t'); if(id==uid){ found=true; continue; } lines.push_back(line); }
 ofstream o(ACC_FILE, ios::trunc); for(auto &l: lines) o<<l<<"\n"; return found; }
static bool updateAccountPW(const string &uid, const string &newpw){ ifstream f(ACC_FILE); vector<Account> arr; string line; bool ok=false; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string id,pw,privs,name; getline(ss,id,'\t'); getline(ss,pw,'\t'); getline(ss,privs,'\t'); getline(ss,name,'\t'); Account a{ id,pw,name, privs.empty()?1:stoi(privs)}; if(id==uid){ a.pw=newpw; ok=true; } arr.push_back(a); }
 ofstream o(ACC_FILE, ios::trunc); for(auto &a: arr) o<<a.id<<'\t'<<a.pw<<'\t'<<a.priv<<'\t'<<a.name<<"\n"; return ok; }

// books.dat: isbn\tname\tauthor\tkeyword\tprice\tstock\n
static optional<Book> findBook(const string &isbn){ ifstream f(BOOK_FILE); string line; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string isb,name,auth,key,price,stock; getline(ss,isb,'\t'); if(isb!=isbn) continue; getline(ss,name,'\t'); getline(ss,auth,'\t'); getline(ss,key,'\t'); getline(ss,price,'\t'); getline(ss,stock,'\t'); Book b; b.isbn=isb; b.name=name; b.author=auth; b.keyword=key; b.price=price.empty()?0:stod(price); b.stock=stock.empty()?0:stoi(stock); return b; } return {}; }
static bool bookExistsISBN(const string &isbn){ return findBook(isbn).has_value(); }
static void writeBookAppend(const Book &b){ ofstream f(BOOK_FILE, ios::app); f<<b.isbn<<'\t'<<b.name<<'\t'<<b.author<<'\t'<<b.keyword<<'\t'; f.setf(std::ios::fixed); f<<setprecision(2)<<b.price<<'\t'<<b.stock<<"\n"; }
static bool updateOrInsertBook(const Book &b){ // upsert by ISBN
 ifstream f(BOOK_FILE); vector<Book> arr; string line; bool updated=false; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string isb,name,auth,key,price,stock; getline(ss,isb,'\t'); getline(ss,name,'\t'); getline(ss,auth,'\t'); getline(ss,key,'\t'); getline(ss,price,'\t'); getline(ss,stock,'\t'); Book cur; cur.isbn=isb; cur.name=name; cur.author=auth; cur.keyword=key; cur.price=price.empty()?0:stod(price); cur.stock=stock.empty()?0:stoi(stock); if(cur.isbn==b.isbn){ arr.push_back(b); updated=true; } else arr.push_back(cur);
 }
 if(!updated) arr.push_back(b);
 ofstream o(BOOK_FILE, ios::trunc); o.setf(std::ios::fixed); for(auto &x: arr){ o<<x.isbn<<'\t'<<x.name<<'\t'<<x.author<<'\t'<<x.keyword<<'\t'<<setprecision(2)<<x.price<<'\t'<<x.stock<<"\n"; }
 return true; }
static bool replaceISBN(const string &oldisbn, const string &newisbn){ ifstream f(BOOK_FILE); vector<Book> arr; string line; bool ok=false; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string isb,name,auth,key,price,stock; getline(ss,isb,'\t'); getline(ss,name,'\t'); getline(ss,auth,'\t'); getline(ss,key,'\t'); getline(ss,price,'\t'); getline(ss,stock,'\t'); Book cur; cur.isbn=isb; cur.name=name; cur.author=auth; cur.keyword=key; cur.price=price.empty()?0:stod(price); cur.stock=stock.empty()?0:stoi(stock); if(isb==oldisbn){ cur.isbn=newisbn; ok=true; } arr.push_back(cur); }
 ofstream o(BOOK_FILE, ios::trunc); o.setf(std::ios::fixed); for(auto &x: arr){ o<<x.isbn<<'\t'<<x.name<<'\t'<<x.author<<'\t'<<x.keyword<<'\t'<<setprecision(2)<<x.price<<'\t'<<x.stock<<"\n"; } return ok; }

static void log_op(const string &s){ ofstream f(OPS_FILE, ios::app); f<<s<<"\n"; }
static void log_fin(double income, double expense){ ofstream f(FIN_FILE, ios::app); f.setf(std::ios::fixed); f<<setprecision(2)<<income<<'\t'<<setprecision(2)<<expense<<"\n"; }

static int currentPriv(){ return stackv.empty()?0:stackv.back().priv; }
static string currentUser(){ return stackv.empty()?string(""):stackv.back().userid; }
static string selectedISBN(){ return stackv.empty()?string(""):stackv.back().selected_isbn; }

// Root auto init
static void ensureRoot(){ if(accountExists("root")) return; Account a{ "root","sjtu","root",7}; writeAccount(a); }

// Keyword helpers
static bool keywordHasMultiple(const string &kw){ return kw.find('|')!=string::npos; }
static bool keywordHasDuplicateSegments(const string &kw){ if(kw.empty()) return false; vector<string> segs; string cur; stringstream ss(kw); while(getline(ss,cur,'|')) segs.push_back(cur); set<string> st; for(auto &x: segs){ if(x.empty()) return true; if(st.count(x)) return true; st.insert(x);} return false; }

// show filter parsing and sorting by ISBN
static void cmd_show(const vector<string>& tokens){ if(currentPriv()<1){ cout<<"Invalid\n"; return; }
 // tokens: show [optional filter]
 // Collect all books, filter while reading, then sort by ISBN asc. Streaming is acceptable for tens of thousands.
 string mode=""; string value=""; if(tokens.size()>1){ // parse -ISBN=xxx or -name="..."
 string t = tokens[1]; if(!starts_with(t, "-")){ cout<<"Invalid\n"; return; }
 size_t eq = t.find('='); if(eq==string::npos){ cout<<"Invalid\n"; return; }
 mode = t.substr(1, eq-1); value = t.substr(eq+1);
 if(value.size()==0){ cout<<"Invalid\n"; return; }
 if(mode=="ISBN"){ if(!isLegalISBN(value)){ cout<<"Invalid\n"; return; } }
 else if(mode=="name"){ if(value.size()<2 || value.front()!='"' || value.back()!='"'){ cout<<"Invalid\n"; return; } value=value.substr(1,value.size()-2); if(!isLegalNameAuth(value)){ cout<<"Invalid\n"; return; } }
 else if(mode=="author"){ if(value.size()<2 || value.front()!='"' || value.back()!='"'){ cout<<"Invalid\n"; return; } value=value.substr(1,value.size()-2); if(!isLegalNameAuth(value)){ cout<<"Invalid\n"; return; } }
 else if(mode=="keyword"){ if(value.size()<2 || value.front()!='"' || value.back()!='"'){ cout<<"Invalid\n"; return; } value=value.substr(1,value.size()-2); if(!isLegalKeyword(value)){ cout<<"Invalid\n"; return; } if(keywordHasMultiple(value)){ cout<<"Invalid\n"; return; } }
 else { cout<<"Invalid\n"; return; }
 }
 vector<Book> outs; ifstream f(BOOK_FILE); string line; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string isb,name,auth,key,price,stock; getline(ss,isb,'\t'); getline(ss,name,'\t'); getline(ss,auth,'\t'); getline(ss,key,'\t'); getline(ss,price,'\t'); getline(ss,stock,'\t'); Book b; b.isbn=isb; b.name=name; b.author=auth; b.keyword=key; b.price=price.empty()?0:stod(price); b.stock=stock.empty()?0:stoi(stock);
 bool ok=true; if(!mode.empty()){
 if(mode=="ISBN") ok = (b.isbn==value);
 else if(mode=="name") ok = (b.name==value);
 else if(mode=="author") ok = (b.author==value);
 else if(mode=="keyword") ok = (b.keyword==value);
 }
 if(ok) outs.push_back(b);
 }
 sort(outs.begin(), outs.end(), [](const Book&a,const Book&b){ return a.isbn < b.isbn; });
 if(outs.empty()){ cout<<"\n"; return; }
 cout.setf(std::ios::fixed);
 for(size_t i=0;i<outs.size();++i){ cout<<outs[i].isbn<<"\t"<<outs[i].name<<"\t"<<outs[i].author<<"\t"<<outs[i].keyword<<"\t"<<setprecision(2)<<outs[i].price<<"\t"<<outs[i].stock<<"\n"; }
}

static void cmd_buy(const vector<string>& tokens){ if(currentPriv()<1){ cout<<"Invalid\n"; return; } if(tokens.size()!=3){ cout<<"Invalid\n"; return; }
 string isbn=tokens[1], qtys=tokens[2]; if(!isLegalISBN(isbn)||!isLegalInt(qtys)){ cout<<"Invalid\n"; return; }
 auto ob = findBook(isbn); if(!ob){ cout<<"Invalid\n"; return; }
 Book b=*ob; int q=stoi(qtys); if(q<=0){ cout<<"Invalid\n"; return; }
 if(b.stock<q){ cout<<"Invalid\n"; return; }
 b.stock-=q; updateOrInsertBook(b); double total = b.price * q; cout.setf(std::ios::fixed); cout<<setprecision(2)<<total<<"\n"; log_fin(total,0.00); log_op(currentUser()+" buy "+isbn+" "+qtys); }

static void cmd_select(const vector<string>& tokens){ if(currentPriv()<3){ cout<<"Invalid\n"; return; } if(tokens.size()!=2){ cout<<"Invalid\n"; return; }
 string isbn=tokens[1]; if(!isLegalISBN(isbn)){ cout<<"Invalid\n"; return; }
 if(!bookExistsISBN(isbn)){ Book b; b.isbn=isbn; b.name=""; b.author=""; b.keyword=""; b.price=0; b.stock=0; writeBookAppend(b); }
 stackv.back().selected_isbn=isbn; }

static void cmd_modify(const vector<string>& tokens){ if(currentPriv()<3){ cout<<"Invalid\n"; return; }
 if(selectedISBN().empty()){ cout<<"Invalid\n"; return; }
 // parse multiple -key=value pairs
 set<string> seen; Book b = findBook(selectedISBN()).value(); Book newb=b; for(size_t i=1;i<tokens.size();++i){ string t=tokens[i]; if(!starts_with(t,"-")){ cout<<"Invalid\n"; return; } size_t eq=t.find('='); if(eq==string::npos){ cout<<"Invalid\n"; return; } string key=t.substr(1,eq-1); string val=t.substr(eq+1); if(seen.count(key)) { cout<<"Invalid\n"; return; } seen.insert(key);
 if(key=="ISBN"){ if(!isLegalISBN(val)){ cout<<"Invalid\n"; return; } if(val==b.isbn){ cout<<"Invalid\n"; return; } if(bookExistsISBN(val)){ cout<<"Invalid\n"; return; } newb.isbn=val; }
 else if(key=="name"){ if(val.size()<2||val.front()!='"'||val.back()!='"'){ cout<<"Invalid\n"; return; } val=val.substr(1,val.size()-2); if(!isLegalNameAuth(val)){ cout<<"Invalid\n"; return; } newb.name=val; }
 else if(key=="author"){ if(val.size()<2||val.front()!='"'||val.back()!='"'){ cout<<"Invalid\n"; return; } val=val.substr(1,val.size()-2); if(!isLegalNameAuth(val)){ cout<<"Invalid\n"; return; } newb.author=val; }
 else if(key=="keyword"){ if(val.size()<2||val.front()!='"'||val.back()!='"'){ cout<<"Invalid\n"; return; } val=val.substr(1,val.size()-2); if(!isLegalKeyword(val)){ cout<<"Invalid\n"; return; } if(keywordHasDuplicateSegments(val)){ cout<<"Invalid\n"; return; } newb.keyword=val; }
 else if(key=="price"){ if(!isLegalMoney(val)){ cout<<"Invalid\n"; return; } newb.price = stod(val); }
 else { cout<<"Invalid\n"; return; }
 }
 // write back; if ISBN changed, replace entry
 if(newb.isbn!=b.isbn){ replaceISBN(b.isbn,newb.isbn); // other fields remain old; then update fields on new isbn
 }
 updateOrInsertBook(newb); // maintain selection
 stackv.back().selected_isbn=newb.isbn; }

static void cmd_import(const vector<string>& tokens){ if(currentPriv()<3){ cout<<"Invalid\n"; return; } if(tokens.size()!=3){ cout<<"Invalid\n"; return; }
 if(selectedISBN().empty()){ cout<<"Invalid\n"; return; }
 string qtys=tokens[1], totals=tokens[2]; if(!isLegalInt(qtys)||!isLegalMoney(totals)){ cout<<"Invalid\n"; return; }
 int q=stoi(qtys); double tot=stod(totals); if(q<=0||tot<=0){ cout<<"Invalid\n"; return; }
 auto ob=findBook(selectedISBN()); if(!ob){ cout<<"Invalid\n"; return; }
 Book b=*ob; b.stock+=q; updateOrInsertBook(b); log_fin(0.00, tot); log_op(currentUser()+" import "+b.isbn+" "+qtys+" "+totals); }

static void cmd_show_finance(const vector<string>& tokens){ if(currentPriv()<7){ cout<<"Invalid\n"; return; }
 long long limit=-1; if(tokens.size()==3){ string c=tokens[2]; if(!isLegalNonnegInt(c)){ cout<<"Invalid\n"; return; } limit=stoll(c); }
 vector<pair<double,double>> arr; ifstream f(FIN_FILE); string line; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string inc,exp; getline(ss,inc,'\t'); getline(ss,exp,'\t'); double i = inc.empty()?0:stod(inc); double e = exp.empty()?0:stod(exp); arr.emplace_back(i,e); }
 if(limit==0){ cout<<"\n"; return; }
 if(limit> (long long)arr.size()){ cout<<"Invalid\n"; return; }
 long long start = (limit<0)?0: (long long)arr.size()-limit; double I=0,E=0; for(size_t i=start;i<arr.size();++i){ I+=arr[i].first; E+=arr[i].second; }
 cout.setf(std::ios::fixed); cout<<"+ "<<setprecision(2)<<I<<" - "<<setprecision(2)<<E<<"\n"; }

static void cmd_log(){ if(currentPriv()<7){ cout<<"Invalid\n"; return; }
 ifstream f(OPS_FILE); string line; bool any=false; while(getline(f,line)){ cout<<line<<"\n"; any=true; }
 if(!any){ cout<<"\n"; }
}
static void cmd_report_fin(){ if(currentPriv()<7){ cout<<"Invalid\n"; return; }
 ifstream f(FIN_FILE); string line; double I=0,E=0; while(getline(f,line)){ if(line.empty()) continue; stringstream ss(line); string inc,exp; getline(ss,inc,'\t'); getline(ss,exp,'\t'); I += inc.empty()?0:stod(inc); E += exp.empty()?0:stod(exp); }
 cout.setf(std::ios::fixed); cout<<"FINANCE REPORT\nTotal Income: "<<setprecision(2)<<I<<"\nTotal Expenditure: "<<setprecision(2)<<E<<"\n"; }
static void cmd_report_emp(){ if(currentPriv()<7){ cout<<"Invalid\n"; return; }
 ifstream f(OPS_FILE); string line; int cnt=0; while(getline(f,line)) cnt++; cout<<"EMPLOYEE REPORT\nTotal Ops: "<<cnt<<"\n"; }

// Account commands
static void cmd_su(const vector<string>& tokens){ if(tokens.size()<2||tokens.size()>3){ cout<<"Invalid\n"; return; }
 string uid=tokens[1]; if(!isLegalID(uid)){ cout<<"Invalid\n"; return; }
 auto acc=findAccount(uid); if(!acc){ cout<<"Invalid\n"; return; }
 int curp=currentPriv(); string pw=""; if(tokens.size()==3) pw=tokens[2]; if(curp>acc->priv){ // higher priv can omit password
 // ok even if pw omitted
 } else {
 if(pw.empty() || pw!=acc->pw){ cout<<"Invalid\n"; return; }
 }
 stackv.push_back(Session{uid, acc->priv, ""}); }
static void cmd_logout(){ if(currentPriv()<1){ cout<<"Invalid\n"; return; } stackv.pop_back(); }
static void cmd_register(const vector<string>& tokens){ if(tokens.size()!=4){ cout<<"Invalid\n"; return; }
 string uid=tokens[1], pw=tokens[2], uname=tokens[3]; if(!isLegalID(uid)||!isLegalID(pw)||!isLegalUser(uname)){ cout<<"Invalid\n"; return; }
 if(accountExists(uid)){ cout<<"Invalid\n"; return; }
 Account a{uid,pw,uname,1}; writeAccount(a); }
static void cmd_passwd(const vector<string>& tokens){ if(tokens.size()<3||tokens.size()>4){ cout<<"Invalid\n"; return; }
 string uid=tokens[1]; if(!isLegalID(uid)){ cout<<"Invalid\n"; return; }
 auto acc=findAccount(uid); if(!acc){ cout<<"Invalid\n"; return; }
 int curp=currentPriv(); if(curp<1){ cout<<"Invalid\n"; return; }
 if(curp==7){ // may omit current pw
 string newpw = (tokens.size()==3? tokens[2] : tokens[3]); if(!isLegalID(newpw)){ cout<<"Invalid\n"; return; } updateAccountPW(uid,newpw); }
 else { if(tokens.size()!=4){ cout<<"Invalid\n"; return; } string cur=tokens[2], np=tokens[3]; if(!isLegalID(cur)||!isLegalID(np)){ cout<<"Invalid\n"; return; } if(cur!=acc->pw){ cout<<"Invalid\n"; return; } updateAccountPW(uid,np); }
 }
static void cmd_useradd(const vector<string>& tokens){ if(tokens.size()!=5){ cout<<"Invalid\n"; return; }
 if(currentPriv()<3){ cout<<"Invalid\n"; return; }
 string uid=tokens[1], pw=tokens[2], privs=tokens[3], uname=tokens[4]; if(!isLegalID(uid)||!isLegalID(pw)||!isLegalPriv(privs)||!isLegalUser(uname)){ cout<<"Invalid\n"; return; }
 int p=stoi(privs); if(p>=currentPriv()){ cout<<"Invalid\n"; return; }
 if(accountExists(uid)){ cout<<"Invalid\n"; return; }
 Account a{uid,pw,uname,p}; writeAccount(a); }
static void cmd_delete(const vector<string>& tokens){ if(tokens.size()!=2){ cout<<"Invalid\n"; return; }
 if(currentPriv()<7){ cout<<"Invalid\n"; return; }
 string uid=tokens[1]; if(!isLegalID(uid)){ cout<<"Invalid\n"; return; }
 if(!accountExists(uid)){ cout<<"Invalid\n"; return; }
 if(accountLoggedIn(uid)){ cout<<"Invalid\n"; return; }
 deleteAccount(uid); }

int main(){ ios::sync_with_stdio(false); cin.tie(nullptr);
 ensureRoot();
 string line; while(true){ if(!std::getline(cin,line)) break; string t = trim(line); if(t.empty()){ continue; }
 // tokenize respecting quotes
 auto tokenize = [](const string &s){
 vector<string> out; string cur; bool inq=false; for(char c: s){
 if(c=='"'){ inq=!inq; cur.push_back(c); }
 else if(c==' ' && !inq){ if(!cur.empty()){ out.push_back(cur); cur.clear(); } }
 else { cur.push_back(c); }
 }
 if(!cur.empty()) out.push_back(cur); return out; };

 vector<string> tokens = tokenize(t);
 if(tokens.empty()) continue;
 string cmd=tokens[0]; if(cmd=="quit"||cmd=="exit") break;
 else if(cmd=="show" && tokens.size()>=2 && tokens[1]=="finance") cmd_show_finance(tokens);
 else if(cmd=="su") cmd_su(tokens);
 else if(cmd=="logout") cmd_logout();
 else if(cmd=="register") cmd_register(tokens);
 else if(cmd=="passwd") cmd_passwd(tokens);
 else if(cmd=="useradd") cmd_useradd(tokens);
 else if(cmd=="delete") cmd_delete(tokens);
 else if(cmd=="show") cmd_show(tokens);
 else if(cmd=="buy") cmd_buy(tokens);
 else if(cmd=="select") cmd_select(tokens);
 else if(cmd=="modify") cmd_modify(tokens);
 else if(cmd=="import") cmd_import(tokens);
 else if(cmd=="log") cmd_log();
 else if(cmd=="report"){
 if(tokens.size()!=2){ cout<<"Invalid\n"; }
 else if(tokens[1]=="finance") cmd_report_fin(); else if(tokens[1]=="employee") cmd_report_emp(); else cout<<"Invalid\n";
 }
 else { cout<<"Invalid\n"; }
 }
 return 0; }
