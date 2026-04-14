/*
 * @lc app=leetcode.cn id=977 lang=cpp
 * @lcpr version=30204
 *
 * [977] 有序数组的平方
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
    vector<int> sortedSquares(vector<int>& nums) {
        vector<int>res(nums.size(),0);
        int i = nums.size()-1;
        for(int left=0,right=nums.size()-1;left<=right;){
            int leftMul = nums[left] * nums[left];
            int rightMul = nums[right]*nums[right];
            if(leftMul > rightMul){
                res[i--]=leftMul;
                left++;
            }else{
                res[i--]=rightMul;
                --right;
            }
        }
        return res;
    }
};
// @lc code=end



/*
// @lcpr case=start
// [-4,-1,0,3,10]\n
// @lcpr case=end

// @lcpr case=start
// [-7,-3,2,3,11]\n
// @lcpr case=end

 */

