// @lcpr-before-debug-begin




// @lcpr-before-debug-end

/*
 * @lc app=leetcode.cn id=844 lang=cpp
 * @lcpr version=30204
 *
 * [844] 比较含退格的字符串
 */


// @lcpr-template-start
using namespace std;
#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <deque>
#include <functional>
#include <iostream>
#include <list>
#include <queue>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
// @lcpr-template-end
// @lc code=start
class Solution {
public:
    bool backspaceCompare(string s, string t) {
        getStr(s);
        getStr(t);
        if(t.size()==0 && s.size() == 0) return true;
       else{
           for(int i=0; i<max(t.size(), s.size()); i++) {
               if(t[i] != s[i]){
                   return false;
               }
           }
       }
       return true;
    }

private:
    void getStr(string &str){
        int i=0;
        for(int j=0;j<str.length();j++){
            if(str[j]!='#'){
                str[i++] = str[j];
            }else if(str[j]=='#' && i!=0){
                --i;
            }
        }
        str.resize(i);
    }
};
// @lc code=end



/*
// @lcpr case=start
// "ab#c"\n"ad#c"\n
// @lcpr case=end

// @lcpr case=start
// "ab##"\n"c#d#"\n
// @lcpr case=end

// @lcpr case=start
// "a#c"\n"b"\n
// @lcpr case=end

 */

