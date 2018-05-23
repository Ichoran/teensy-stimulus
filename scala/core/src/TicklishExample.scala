package lab.kerrr.ticklish

import java.time._
import scala.concurrent._

object TicklishExample {
  def main(args: Array[String]) {
    if (args.length > 0) throw new Exception("Sorry, I don't understand any arguments.")

    println("Hello!  We're going to find a Teensy 3.x board running Ticklish 2.x!")
    val names = Ticklish.possibilities
    println
    println("We found ${names.length} ports: ${names.mkString(", ")}")
    val tkh = Ticklish.openFirstWorking match {
      case None =>
        println("Uh-oh, we didn't find anything.  This is bad.  Are you sure it's plugged in???")
        throw new Exception("No Teensy running Ticklish found!")
      case Some(t) => t
    }
    println
    println(f"Very good!  We got ${tkh.portname}, opened it, and verified it works.")
    println
    println(f"Let's check the ID.")
    println(f"  Hello, I'm Ticklish and my name is ${tkh.id}!")
    println
    println(f"Now let's set up a protocol.")
    println(f"  First we'll wait for 3 seconds.")
    println(f"  Then we'll blink once a second (half on, half off) 10 times")
    println(f"  Then we'll wait for 5 more seconds.")
    println(f"  Then we'll do five triple-blinks every two seconds")
    println(f"    (A triple-blink is 100 ms on, 200 ms off.)")
    println
    println("Let's set up.")
    tkh.clear
    println(f"Cleared previous state.  We are ready to program: ${tkh.state == TicklishState.Program}")
    val partOne = Ticklish.Digital(3, 1, 0.5, 10) match {
      case None => throw new Exception("Couldn't create the first part of the stimulus???")
      case Some(x) => x
    }
    val partTwo = Ticklish.Digital(5, 2, 5, 0.3, 0.1, 3) match {
      case None => throw new Exception("Couldn't create the second part of the stimulus???")
      case Some(x) => x
    }
    println(f"Stimulus commands:")
    println(f"  ${partOne.command}")
    println(f"  ${partTwo.command}")
    tkh.set('X', partOne :: partTwo :: Nil)
    println
    println(f"All set.  Were there errors?  ${tkh.isError}")
    if (tkh.isError) { tkh.clear; tkh.disconnect; throw new Exception("Ack!  An error!  Fail!") }
    println
    println(f"Let's GO!")
    val timing = tkh.run()
    println
    println(f"Now running; computer and Ticklish board synced.")
    println(f"  Max error estimated as ${timing.window.get(temporal.ChronoUnit.NANOS)/1000} us")
    println
    println(f"Check out the lights for a bit!  We'll wait.")
    Thread.sleep(7000)
    val newtiming = tkh.timesync()
    val expected = timing.boardAt plus Duration.ofNanos(newtiming.stamp - timing.stamp)
    val actual = newtiming.boardAt
    val maxerror = timing.window plus newtiming.window
    val ourerror = if (actual.compareTo(expected) < 0) expected minus actual else actual minus expected
    println(f"Okay, we expect to be ${expected.toString.drop(2)} into the protocol now.")
    println(f"And the board reports: ${actual.toString.drop(2)}")
    println(f"  We thought the error could be as big as ${maxerror.toString.drop(2)}")
    println(f"  And it was actually ${ourerror.toString.drop(2)}")
    println
    println("Okay, let's wait until we're done.")
    var count = 0
    while (tkh.isRun) {
      println("  Not yet!")
      Thread.sleep(2000)
      count += 1
      if (count*2000000L > partOne.duration + partTwo.duration) {
        println("    Um...we didn't stop???  Aborting.")
        tkh.clear()
      }
    }
    println("Done!")
    println
    println("Cleaning up!")
    tkh.clear
    tkh.disconnect
    println
    println("All done.")
  }
}
