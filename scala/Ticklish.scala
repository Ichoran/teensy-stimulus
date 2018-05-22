package lab.kerrr.ticklish

import java.time._
import java.nio.ByteBuffer

import scala.util._
import scala.util.control.NonFatal

import jssc._

class Ticklish private[ticklish] (val portname: String) {
  private var port: SerialPort = null
  private var cachedIdentity: String = null
  private var myId: String = null
  private var myVersion: String = null
  private val received = new java.util.concurrent.LinkedBlockingDeque[ByteBuffer]
  private var working: ByteBuffer = null
  private val myUnderlying = new Array[Byte](64)
  private val myBuffer = ByteBuffer.wrap(myUnderlying)
  private var completed: Option[String] = None
  private var patience = 500
  private val patienceUnit = java.util.concurrent.TimeUnit.MILLISECONDS  // DO NOT change this!  `readBytes` is always milliseconds!

  def isConnected: Boolean = port ne null

  def connect() {
    if (!isConnected) {
      port = new SerialPort(portname)
      port.openPort ||
        (throw new Exception(f"Could not open port $portname"))
      port.setParams(SerialPort.BAUDRATE_115200, SerialPort.DATABITS_8, SerialPort.STOPBITS_1, SerialPort.PARITY_NONE) ||
        (throw new Exception(f"Could not set parameters for port $portname"))
      port.setFlowControlMode(SerialPort.FLOWCONTROL_NONE) || 
        (throw new Exception(f"Could not set flow control for $portname"))
      port.addEventListener(
        new SerialPortEventListener {
          def serialEvent(e: SerialPortEvent) {
            if (e.getEventType == SerialPortEvent.RXCHAR) received.add(ByteBuffer wrap port.readBytes(e.getEventValue, patience))
          }
        }
      )
    }
  }

  def disconnect() {
    if (port ne null) port.closePort
    port = null
    cachedIdentity = null
    myVersion = null
    myId = null
    received.clear
  }

  def advanceLine(): Boolean =
    if (!isConnected) throw new Exception("Cannot read from a closed port")
    else {
      val t0 = System.nanoTime/1000000L
      var t1 = t0
      var foundLF = false
      while (t1 - t0 < patience && !foundLF && myBuffer.remaining > 0) {
        if (working eq null) {
          working = received.poll(patience - (t1 - t0), patienceUnit)
          if (working eq null) { 
            myBuffer.clear
            completed = None
            return false
          }       
        }
        t1 = System.nanoTime/1000000L
        while (working.hasRemaining && !foundLF) {
          val b = working.get
          if (b == '\n') {
            foundLF = true
            myBuffer.flip
          }
          else myBuffer.put(b)
        }
        if (!working.hasRemaining) working = null
      }
      if (foundLF) {
        completed = Some(new String(myUnderlying, 0, myBuffer.remaining, "ASCII"));
        myBuffer.clear
      }
      else completed = None
      foundLF
    }

  def currentLine(): Option[String] = completed

  def read(): String = currentLine() match {
    case Some(s) =>
      completed = None
      if (!s.startsWith("$") && !s.startsWith("~")) throw new Exception(s"String did not start with $$ or ~: $s")
      s
    case None =>
      if (advanceLine()) read()
      else throw new Exception(s"No complete string read within $patience ms")
  }
  
  def write(s: String) {
    if (!isConnected) throw new Exception("Cannot write to a closed port.")
    port.writeString(s + "\n")
  }

  def query(s: String) = { write(s); read() }

  def isTicklish(): Boolean = 
    try { 
      if (cachedIdentity == null) cachedIdentity = query("~?")
      TicklishUtil.isTicklish(cachedIdentity)
    } catch { case t if NonFatal(t) => false }

  def id(): String = {
    if (cachedIdentity == null) cachedIdentity = query("~?")
    if (myId == null) myId = TicklishUtil.decodeName(cachedIdentity)._1
    myId
  }

  def version(): String = {
    if (cachedIdentity == null) cachedIdentity = query("~?")
    if (myVersion == null) myVersion = TicklishUtil.decodeName(cachedIdentity)._2
    myVersion
  }

  def getDrift(): Double = TicklishUtil.decodeDrift(query("~^+00000000?")).getOrElse(Double.NaN)
  def setDrift(error: Double, toEEPROM: Boolean = false): Option[Boolean] =
    if (isError) None
    else {
      try {
        val ans = query("~^" + TicklishUtil.encodeDrift(error) + (if (toEEPROM) "!" else "."))
        TicklishUtil.decodeDrift(ans).map(_ => ans.endsWith("!"))
      } catch { case e if NonFatal(e) => None }   
    }
  def fixDrift(first: Ticklish.Timed, second: Ticklish.Timed, minimumError: Double = 1e-6, toEEPROM: Boolean = false): Option[Boolean] =
    if (isError) None
    else {
      val jitter = java.time.Duration.between(first.zero, second.zero)
      val elapsed = second.boardAt minus first.boardAt
      val measured = (jitter.getSeconds + 1e-9*jitter.getNano)/(elapsed.getSeconds + 1e-9*elapsed.getNano)
      val already = getDrift()
      if (already.isNaN) None
      else if (math.abs(measured) < minimumError) Some(false)
      else setDrift(measured + already, toEEPROM)
    }
  def zeroDrift(): Boolean = try { query("~^+00000000."); true } catch { case t if NonFatal(t) => false  }

  def state(): TicklishState = TicklishUtil.decodeState(query("~@"))

  def ping(): Boolean = try{ query("~'") == "$"; true } catch { case t if NonFatal(t) => false }

  def clear() { write("~.") }

  def isError(): Boolean = try { state == TicklishState.Errored } catch { case t if NonFatal(t) => true }
  def isProg(): Boolean  = try { state == TicklishState.Program } catch { case t if NonFatal(t) => false }
  def isRun(): Boolean   = try { state == TicklishState.Running } catch { case t if NonFatal(t) => false }
  def isDone(): Boolean  = try { state == TicklishState.AllDone } catch { case t if NonFatal(t) => false }

  def timesync(): Ticklish.Timed = {
    val before = LocalDateTime.now
    val t0 = System.nanoTime
    val ans = query("~#")
    val t1 = System.nanoTime
    if (!TicklishUtil.isTimeReport(ans)) throw new Exception(f"Run error instead of timing info: $ans")
    val current = TicklishUtil.decodeTime(ans)
    val delta = (if (t1 == t0) 5000000 else t1 - t0)   // If they appear the same, recklessly guess ~5 ms difference
    Ticklish.Timed(before minus current, Duration.ofNanos(t1 - t0), t0, current)
  }

  def getLogic(): Option[Boolean] =
    if (channel >= 'X') None
    else {
      try { Some(TicklishUtil.decodeVoltage(query(f"~$channel?")) > 2) }
      catch { case _: Exception => None }
    }

  def getVoltage(): Option[Float] =
    if (channel >= 'K') None
    else {
      try { Some(TicklishUtil.decodeVoltage(query(f"~$channel?"))) }
      catch { case _: Exception => None }
    }

  def getTempCHumidPct(): Option[(Float, Float)] =
    if (channel < 'K' || channel > 'W') None
    else {
      try {
        TicklishUtil.decodeBitsTH(query(f"~$channel|?")).filter{ _ =>
          TicklishUtil.decodeTime(query(f"~$channel|#")).compareTo(Duration.ZERO) > 0
        }
      }
      catch { case _: Exception => None }
    }

  def set(channel: Char, dtl: Ticklish.Digital, following: Boolean) {
    if (channel < 'A' || channel > 'X') throw new Exception(f"Invalid channel: $channel")
    if (dtl.pulseread && (channel < 'K' || channel > 'W')) throw new Exception(f"Invalid channel for pulse read: $channel")
    if (following) write(f"~$channel&");
    write(f"~$channel${dtl.command}")
    if (isError()) throw new Exception(f"Failed to set channel $channel")
  }

  def set(channel: Char, dtl: Ticklish.Digital) { set(channel, dtl, false) }

  def set(channel: Char, dtls: Seq[Ticklish.Digital]) { 
    for { (dtl, i) <- dtls.zipWithIndex } set(channel, dtl, i != 0)
  }

  def set(chdts: Seq[(Char, Ticklish.Digital)]) { 
    for { (c, dtl) <- chdts } set(c, dtl)
  }

  def set[A <: Char](chdtss: Seq[(A, Seq[Ticklish.Digital])])(implicit ev: A =:= Char) { 
    for { (c, dts) <- chdtss } set(c, dts)
  }

  def run(): Ticklish.Timed = {
    state match {
      case TicklishState.AllDone =>
        write("~\"")
        if (!ping()) throw new Exception("Would not refresh to run again")
      case TicklishState.Program =>
      case x => throw new Exception(s"Cannot run because state is $x")
    }
    write("~*")
    if (!ping()) throw new Exception("Nonresponsive after issuing run command")
    timesync()
  }

  override def finalize() { try { disconnect() } catch { case t if NonFatal(t) => } }
}
object Ticklish {
  private def managedOpening[U](portname: String)(f: Ticklish => U): Try[Ticklish] =
    Try{ new Ticklish(portname) }.
      map{ t =>
        t.connect()
        val correct = try { t.isTicklish } catch { case e if NonFatal(e) => try{ t.disconnect() } catch { case x if NonFatal(x) => }; throw e }
        if (!correct) throw new Exception(f"Port $portname was not Ticklish")
        try { f(t) } catch { case e if NonFatal(e) => try { t.disconnect() } catch { case x if NonFatal(x) => }; throw e }
        t
      }

  def possibilities: Vector[String] = SerialPortList.getPortNames.toVector

  def openFirstWorking: Option[Ticklish] =
    possibilities.iterator.map(name => managedOpening(name)(identity)).collectFirst{ case Success(x) => x }

  def openAllWorking: Vector[Ticklish] =
    possibilities.iterator.flatMap(name => managedOpening(name)(identity).toOption).toVector

  def apply(portname: String): Try[Ticklish] = managedOpening(portname)(identity)

  def unsafeCreateClosed(portname: String): Ticklish = new Ticklish(portname)

  case class Timed(zero: LocalDateTime, window: Duration, stamp: Long, boardAt: Duration) {}

  class Digital private[ticklish](
    val duration: Long,
    val delay: Long,
    val blockhigh: Long,
    val blocklow: Long,
    val pulsehigh: Long,
    val pulselow: Long,
    val upright: Boolean = true,
    val pulseread: Boolean = false
  ) {
    val blockinterval = blockhigh + blocklow
    val pulseinterval = pulsehigh + pulselow
    val blockduty = blockhigh / blockinterval.toDouble
    val pulseduty = pulsehigh / blockinterval.toDouble
    val blockcount = 
      if (duration == delay) 0.0
      else {
        val live = duration - delay
        val unrounded = {
          if (live < blockhigh) live/blockhigh.toDouble
          else {
            val full = (live - blockhigh) / blockinterval
            val extra = live - blockhigh - blockinterval*full
            1 + full + (if (extra < blocklow) 0.0 else (extra - blocklow) / blockhigh.toDouble)
          }
        }
        if (unrounded < 4.5036e9) math.rint(unrounded * 1e6)/1e6 else unrounded
      }
    val pulses = {
      val full = (blockhigh - pulsehigh) / pulseinterval
      val extra = blockhigh - pulsehigh - pulseinterval*full
      val unrounded = 1 + full + (if (extra < pulselow) 0.0 else (extra - pulselow) / pulsehigh.toDouble)
      if (unrounded < 4.5036e9) math.rint(unrounded * 1e6)/1e6 else unrounded
    }
    override def equals(a: Any) = a match {
      case d: Digital =>
        d.duration == duration &&
        d.delay == delay &&
        d.blockhigh == blockhigh &&
        d.blocklow == blocklow &&
        d.pulsehigh == pulsehigh &&
        d.pulselow == pulselow &&
        d.upright == upright &&
        d.pulseread == pulseread
      case _ => false
    }
    override def hashCode = {
      import scala.util.hashing.MurmurHash3._
      finalizeHash(
        mixLast(
          mix(
            mix(
              mix(
                mix(
                  mix(
                    mix(19195, duration.##),
                    delay.##
                  ),
                  blockhigh.##
                ),
                blocklow.##
              ),
              pulsehigh.##
            ),
            pulselow.##
          ),
          if (upright) 0 else 1
        ),
        7
      )
    }
    private def commanding(c: Char, t: Long) = f"$c${TicklishUtil.encodeTime(Duration.ofNanos(t*1000))}"
    val durationCommand = commanding('t', duration)
    val delayCommand = commanding('d', delay)
    val blockhighCommand = commanding('y', blockhigh)
    val blocklowCommand = commanding('n', blocklow)
    val pulsehighCommand = commanding('p', pulsehigh)
    val pulselowCommand = commanding('q', pulselow)
    val uprightCommand = if (upright) "u" else "i"
    val command =
      Seq(durationCommand, delayCommand, blockhighCommand, blocklowCommand, pulsehighCommand, pulselowCommand).
      map(_ drop 1).
      mkString("=", ";", uprightCommand)
    val runCommand = command.updated(0, ':')
    override val toString = 
      f"$durationCommand $delayCommand $blockhighCommand $blocklowCommand $pulsehighCommand $pulselowCommand $uprightCommand"
  }
  object Digital {
    private[this] final val MaxTime = 99999999000000L
    def apply(delay: Double, interval: Double, high: Double, count: Int): Option[Digital] = {
      val delayus = math.rint(delay*1e6).toLong
      val intervalus = math.rint(interval*1e6).toLong
      val highus = math.rint(high*1e6).toLong
      val totalus = delayus + (if (count > 0) highus + intervalus*(count - 1) else 0)
      if (delay.isNaN || interval.isNaN || high.isNaN) None
      else if (delayus < 0 || delayus > MaxTime) None
      else if (intervalus < 2 || intervalus > MaxTime) None
      else if (highus < 1 || highus >= intervalus || intervalus/(intervalus-highus) >= 1000000) None
      else if (count < 0 || (count == 0 && delayus == 0)) None
      else if (totalus > MaxTime) None
      else Some(new Digital(totalus, delayus, highus, intervalus - highus, highus, intervalus - highus))
    }
    def apply(delay: Double, interval: Double, high: Double, count: Int, invert: Boolean): Option[Digital] = {
      val delayus = math.rint(delay*1e6).toLong
      val intervalus = math.rint(interval*1e6).toLong
      val highus = math.rint(high*1e6).toLong
      val totalus = delayus + (if (count > 0) highus + intervalus*(count - 1) else 0)
      if (delay.isNaN || interval.isNaN || high.isNaN) None
      else if (delayus < 0 || delayus > MaxTime) None
      else if (intervalus < 2 || intervalus > MaxTime) None
      else if (highus < 1 || highus >= intervalus || intervalus/(intervalus-highus) >= 1000000) None
      else if (count < 0 || (count == 0 && delayus == 0)) None
      else if (totalus > MaxTime) None
      else Some(new Digital(totalus, delayus, highus, intervalus - highus, highus, intervalus - highus, !invert))
    }
    def apply(delay: Double, interval: Double, count: Int, pulseinterval: Double, pulsehigh: Double, pulsecount: Int, invert: Boolean = false): Option[Digital] = {
      val dus = math.rint(delay*1e6).toLong
      val intus = math.rint(interval*1e6).toLong
      val pintus = math.rint(pulseinterval*1e6).toLong
      val phus = math.rint(pulsehigh*1e6).toLong
      val hus = phus + (pulsecount - 1)*pintus
      val totalus = dus + (if (count > 0) hus + intus*(count - 1) else 0)
      if (delay.isNaN || interval.isNaN || pulseinterval.isNaN || pulsehigh.isNaN) None
      else if (dus < 0 || dus > MaxTime) None
      else if (intus < 2 || intus > MaxTime) None
      else if (hus < 1 || hus >= intus || intus/(intus-hus) >= 1000000) None
      else if (count < 0 || (count == 0 && dus == 0)) None
      else if (pintus < 2 || pintus > MaxTime) None
      else if (phus < 1 || phus >= pintus || phus/(intus-phus) >= 1000000) None
      else if (count > 0 && pulsecount < 1) None
      else if (totalus > MaxTime) None
      else Some(new Digital(totalus, dus, hus, intus - hus, phus, pintus - phus, !invert))
    }
    def pulseRead(delay: Double, interval: Double, count: Int): Option[Digital] = {
      val delayus = math.rint(delay*1e6).toLong
      val intervalus = math.rint(interval*1e6).toLong
      val highus = 2000000L
      val totalus = delayus + (if (count > 0) highus + intervalus*(count - 1) else 0)
      if (delay.isNaN || interval.isNaN ) None
      else if (delayus < 0 || delayus > MaxTime) None
      else if (intervalus < 2 || intervalus > MaxTime) None
      else if (highus < 1 || highus >= intervalus || intervalus/(intervalus-highus) >= 1000000) None
      else if (count < 0 || (count == 0 && delayus == 0)) None
      else if (totalus > MaxTime) None
      else Some(new Digital(totalus, delayus, highus, intervalus - highus, highus, intervalus - highus, true, true))
    }
  }
}
