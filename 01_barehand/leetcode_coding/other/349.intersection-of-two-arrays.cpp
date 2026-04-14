/*
 * @lc app=leetcode.cn id=349 lang=cpp
 * @lcpr version=30204
 *
 * [349] 两个数组的交集
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
    vector<int> intersection(vector<int>& nums1, vector<int>& nums2) {
        unordered_set<int> bucket(nums1.begin(),nums1.end());
        unordered_set<int> res;
        for(int i=0;i<nums2.size();++i){
            if(bucket.count(nums2[i])==1){
                res.insert(nums2[i]);
            }
        }
        return vector<int>(res.begin(),res.end());
    }
};
// @lc code=end



/*
// @lcpr case=start
// [1,2,2,1]\n[2,2]\n
// @lcpr case=end

// @lcpr case=start
// [4,9,5]\n[9,4,9,8,4]\n
// @lcpr case=end

 */

