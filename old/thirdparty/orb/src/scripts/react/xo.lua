-- Square Component
local function Square(props)
	return createElement("button", { className = "square", onClick = props.onSquareClick }, props.value)
  end

  -- Board Component
  local function Board(props)
	local function handleClick(i)
	  if calculateWinner(props.squares) or props.squares[i + 1] then
		return
	  end
	  local nextSquares = table.move(props.squares, 1, #props.squares, 1, {})
	  nextSquares[i + 1] = props.xIsNext and "X" or "O"
	  props.onPlay(nextSquares)
	end

	local winner = calculateWinner(props.squares)
	local status = winner and "Winner: " .. winner or "Next player: " .. (props.xIsNext and "X" or "O")

	return createElement("div", { className = "board" },
	  createElement("div", { className = "status" }, status),
	  createElement("div", { className = "board-row" },
		Square { value = props.squares[1], onSquareClick = function() handleClick(0) end },
		Square { value = props.squares[2], onSquareClick = function() handleClick(1) end },
		Square { value = props.squares[3], onSquareClick = function() handleClick(2) end }
	  ),
	  createElement("div", { className = "board-row" },
		Square { value = props.squares[4], onSquareClick = function() handleClick(3) end },
		Square { value = props.squares[5], onSquareClick = function() handleClick(4) end },
		Square { value = props.squares[6], onSquareClick = function() handleClick(5) end }
	  ),
	  createElement("div", { className = "board-row" },
		Square { value = props.squares[7], onSquareClick = function() handleClick(6) end },
		Square { value = props.squares[8], onSquareClick = function() handleClick(7) end },
		Square { value = props.squares[9], onSquareClick = function() handleClick(8) end }
	  )
	)
  end

  -- Game Component
  local function Game()
	local history, setHistory = useState({{ nil, nil, nil, nil, nil, nil, nil, nil, nil }})
	local currentMove, setCurrentMove = useState(1)
	local xIsNext = currentMove % 2 == 0
	local currentSquares = history[currentMove]

	local function handlePlay(nextSquares)
	  local nextHistory = table.move(history, 1, #history, 1, {})
	  nextHistory[#nextHistory + 1] = nextSquares
	  setHistory(nextHistory)
	  setCurrentMove(#nextHistory)
	end

	local function jumpTo(nextMove)
	  setCurrentMove(nextMove)
	end

	local moves = {}
	for move, squares in ipairs(history) do
	  local description = move > 0 and "Go to move #" .. move or "Go to game start"
	  moves[#moves + 1] = createElement("li", { key = move },
		createElement("button", { onClick = function() jumpTo(move) end }, description)
	  )
	end

	return createElement("div", { className = "game" },
	  createElement("div", { className = "game-board" },
		Board { xIsNext = xIsNext, squares = currentSquares, onPlay = handlePlay }
	  ),
	  createElement("div", { className = "game-info" },
		createElement("ol", nil, moves)
	  )
	)
  end

  -- calculateWinner function (unchanged)
  local function calculateWinner(squares)
	-- implementation remains unchanged
  end

  return Game
