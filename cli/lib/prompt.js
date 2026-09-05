// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const readline = require('readline')
const { colors } = require('./logger')

function askQuestion(question, autoYes = false) {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  })

  if (autoYes) {
    return new Promise((resolve) => {
      rl.close()
      resolve('')
    })
  }

  return new Promise((resolve) => {
    rl.question(`${colors.cyan}? ${question}${colors.reset} `, (answer) => {
      rl.close()
      resolve(answer.trim())
    })
  })
}

// Interactive single-select list picker. Renders `prompt` then each option,
// with an orange ❯ marking the current selection. Arrow up/down (or k/j) move
// the highlight, Enter confirms, ESC or Ctrl+C aborts (resolves null). Stray
// character input is consumed and ignored so it never corrupts the rendered
// list. `options` is an array of { label, value } objects; the chosen option's
// `value` is returned.
//
// When stdin/stdout are not TTYs (e.g. CI, piped input) it falls back to
// printing a numbered list and asking for a 1..N index via `fallbackAsk`
// (defaults to askQuestion, so tests may inject a stub).
function selectFromList(prompt, options, fallbackAsk = askQuestion) {
  return new Promise((resolve) => {
    const tty = Boolean(process.stdin.isTTY && process.stdout.isTTY)

    // Non-interactive fallback: list options and ask for a 1..N index.
    if (!tty || options.length === 0) {
      if (options.length === 0) {
        resolve(null)
        return
      }
      for (let i = 0; i < options.length; i++) {
        console.log(`  ${i + 1}. ${options[i].label}`)
      }
      fallbackAsk(prompt).then((answer) => {
        const choice = parseInt(answer, 10) - 1
        resolve(choice >= 0 && choice < options.length ? options[choice].value : null)
      })
      return
    }

    const stdout = process.stdout
    let selected = 0
    let started = false

    const print = (text) => stdout.write(text)

    const render = () => {
      print(`${colors.cyan}? ${prompt}${colors.reset}\n`)
      for (let i = 0; i < options.length; i++) {
        print(
          i === selected
            ? `${colors.orange}❯ ${options[i].label}${colors.reset}\n`
            : `  ${options[i].label}\n`,
        )
      }
      print('\x1b[0J')
    }

    const redraw = () => {
      print(`\x1b[${options.length}A`)
      for (let i = 0; i < options.length; i++) {
        print(
          i === selected
            ? `${colors.orange}❯ ${options[i].label}${colors.reset}\n`
            : `  ${options[i].label}\n`,
        )
      }
      print('\x1b[0J')
    }

    const cleanup = () => {
      if (typeof process.stdin.setRawMode === 'function') {
        process.stdin.setRawMode(false)
      }
      process.stdin.removeListener('keypress', onKeypress)
      if (typeof process.stdin.pause === 'function') {
        process.stdin.pause()
      }
    }

    const finish = (value) => {
      // Collapse the whole list so the log stays clean: move back over the
      // prompt + all option lines and erase them. The outcome is reported by
      // the caller right after (e.g. a "✓ Selected ..." line), so there is no
      // need to reprint the selection here.
      const up = 1 + options.length
      print(`\x1b[${up}A`)
      print('\x1b[0J')
      print('\x1b[?25h')
      cleanup()
      resolve(value)
    }

    const onKeypress = (str, key) => {
      key = key || {}
      if (!started) {
        return
      }
      switch (key.name) {
        case 'up':
        case 'k':
          selected = (selected - 1 + options.length) % options.length
          redraw()
          break
        case 'down':
        case 'j':
          selected = (selected + 1) % options.length
          redraw()
          break
        case 'return':
        case 'enter':
          finish(options[selected].value)
          break
        case 'escape':
          // ESC means "the device isn't in the list, I want to quit".
          finish(null)
          break
        case 'c':
          if (key.ctrl) {
            finish(null)
          }
          break
        default:
          // Any other key (letters, numbers, symbols) is deliberately ignored.
          // Reading keypress without a readline interface means nothing is
          // echoed, so the list stays intact.
          break
      }
    }

    // Consume keypress events ourselves instead of a full readline interface.
    // Using readline.createInterface({ terminal: true }) would echo stray
    // characters into the output and corrupt the redrawn list. emitKeypressEvents
    // gives us the parsed key events without any echo.
    readline.emitKeypressEvents(process.stdin)
    if (typeof process.stdin.setRawMode === 'function') {
      process.stdin.setRawMode(true)
    }
    process.stdin.on('keypress', onKeypress)
    process.stdin.resume()

    // Hide the terminal cursor while the list is active so it doesn't look
    // like an inline input prompt.
    print('\x1b[?25l')
    render()
    started = true
  })
}

function askPassword(question) {
  const tty = Boolean(process.stdin.isTTY && process.stdout.isTTY)
  if (!tty) {
    return askQuestion(question)
  }
  return new Promise((resolve) => {
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      terminal: true,
    })
    // Mute everything EXCEPT the first write (the question itself).
    // rl.question() renders its prompt via _writeToOutput, so muting
    // unconditionally swallows the question too and the user sees nothing.
    const originalWrite = rl._writeToOutput.bind(rl)
    let firstWrite = true
    rl._writeToOutput = (str) => {
      if (firstWrite) {
        firstWrite = false
        originalWrite(str)
      }
    }
    rl.question(`${colors.cyan}? ${question}${colors.reset} `, (answer) => {
      rl.close()
      // Move to next line since Enter was swallowed too.
      process.stdout.write('\n')
      resolve(answer.trim())
    })
  })
}

module.exports = { askQuestion, askPassword, selectFromList }
