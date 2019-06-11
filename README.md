# socketProject
* This is a mini project that build with socket programming with C language.
* A player can join at anytime. Each time a player exits, his record will not be kept.

**Stpes to play it:**
1. git clone the source code to your machine. And `cd` to this project directory.

2. In your terminal, type `make` to compile the code.

3. `./wordsrv dictionary.txt` to run the server. Choose 'Allow'.

   ![Screen Shot 2019-06-11 at 9.28.29 PM](/Users/xuwang/Desktop/Screen Shot 2019-06-11 at 9.28.29 PM.png)

4. open another terminal and type: `nc -c localhost 52949`; or you can replace `localhost` with `127.0.0.1`; Here, i open two player windows.![Screen Shot 2019-06-11 at 9.46.49 PM](/Users/xuwang/Desktop/Screen Shot 2019-06-11 at 9.46.49 PM.png)

5. Now, you can just follow the instructions appeared on your terminal!

6. You can as many terminal window as you want, and do step 4. This represents a new player.

**How to play the game:**

* This game runs on a rolling basis. Everytime a new player joins, he will be immidiately be able to play the game with other players. 
* At the beginning of each round, a word will be randomly chosen from dictionary.txt.
* When it's a player's turn, he will take a guess of one letter of the word. 
* If a player wins a guess, he keeps the turn. Otherwise, next player in order list get the turn.
* All players share a **fixed** number of chances for wrong guesses. (You can change this number in gameplay.h, line 7)
* ![Screen Shot 2019-06-11 at 10.08.36 PM](/Users/xuwang/Desktop/Screen Shot 2019-06-11 at 10.08.36 PM.png)
* The game either ends when all guesses runs out **or** one player input the correct final letter of the word.
* Enjoy!