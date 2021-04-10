
/*
length=21 ADCO      12  =ADSC
length=16 OPTARIF   4   "BASE"
length=13 ISOUSC    2   =PREF/200
length=18 BASE      9   =EAIT + OFFSET : valeur piquée du compteur linky à laquelle on ajoute l'offset du vieux compteur
length=13 PTEC      4   "TH.."
length=13 IINST     3   =IRMS1
length=12 ADPS      3   =IRMS1 si IINST > ISOUSC (avec IR = 3KVA/200 soit 15A), sinon champ absent
length=12 IMAX      3   090
length=14 PAPP      5   =SINSTI
length=11 HHPHC     1   "A"
length=19 MOTDETAT  6   "000000"

Total max length = 164 including STX and ETX

Start bit: 1 - data: 7 bits - parity: even - stop bit : 1
*/

const char STX = 0x02; //Start of TeXt used as "Start Frame" flag
const char ETX = 0x03; //End of TeXt used as "End Frame" flag
const char EOT = 0x04; //End Of Transmission
const char LF = '\n'; //Line Feed used as "Start Group" flag
const char CR = '\r'; //Carriage Return used as "End of Group" flag
const char SPC = ' '; //Space used as "Field Separator" flag in history mode
const char TAB = '\t'; //Space used as "Field Separator" flag in standard mode (Linky)

boolean groupStringComplete = false; //flag to indicate that a group has been fully retrieved
String groupString; // buffer to store a group
long eait = 0; // global variable in which the cumulative produced energy will be stored
String papp = "0";
String ptec = "HP.. ";
const unsigned long offset = 18769000; // value of the previous ENEDIS electric production counter
String nextFrame;
String currentFrame;

// data IN pin is 2 => the bit to send which comes from pin 1 (TX from hard serial)
// data OUT pin is 11 => the modulated signal is there
byte pinIn = 2;
byte pinOutA = 11;

// the function which traps TIMER2 interrupts
void toogle(){
  if (digitalRead(pinIn) == HIGH) {
    //stop timer
    TCCR2A &= B10111111;
    TCNT2 = 0;
  } else
    TCCR2A |= B01000000;
}

void setup() {
  //shutdown led 13
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
  
  //for our ASK modulation
  pinMode(pinOutA, OUTPUT);
  pinMode(pinIn, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinIn),toogle, CHANGE);
  
  TCCR2A = _BV(COM2A0) | _BV(WGM21); // mode 2
  TCCR2B = _BV(CS20); //no prescaling
  OCR2A = 159;
  //this stops the timer
  TCCR2A &= B10111111;
  TCNT2 = 0 ;
  
  //allocate some memory for the buffer that will store the group
  groupString.reserve(128);
  nextFrame.reserve(256);
  currentFrame.reserve(256);
  
  //Open serial towards ConsoSpy : 1200 bauds, data 7, parity Even, 1 stop bit
  Serial.begin(1200, SERIAL_7E1);  

}
 
void loop() {
  //handle byte received from Linky electric counter if any
  linkySerialEvent();

  //do something with data received when appropriate
  if(groupStringComplete) {
    //process the group
    if (groupString.length() > 0)
      processGroup();
        
    //reset so that a new group can be read 
    groupStringComplete = false;
    groupString = "";
  } 
  makeFrame();
  sendFrame();
}

void linkySerialEvent() {
  while (Serial.available()) {
    // read the new char
    char inChar = (char)Serial.read() & 0x7F; // & 0x7F to remove bit 7 otherwise we get a bad char (including parity on bit 7)

    
    if (inChar == LF || inChar == STX || inChar == ETX) {
      groupString = "";
    } else if (inChar == CR) {
        // if the incoming character is a carriage return, set a flag so the main loop can
        // do something about it:
        groupStringComplete = true;
        break;
    } else if (groupString.length() > 110) {
        // probably transmission errors, kill buffer so that we don't eat all memory
        // ppointe field length = 98 so LF+"ppointe"+TAB+"98 characters"+TAB+CHECKSUM+CR = 1+7+1+98+1+1+1= 110
        groupString = "";
    } else {
        // add the char to the group string
        groupString += inChar;
    }
  }
}

/*
We just want to extract the EAIT value in the frame we received
The other groups will not be taken into account (no need for the purpose of this application but YOU can if needed !)
At this step, LF and CR have been previously removed and the group is only made of: 
LABEL* TAB* DATA* TAB* CHECKSUM 
or
LABEL* TAB* DATE* TAB* DATA* TAB* CHECKSUM
In both cases the checksum takes into account fields with the star

Finally, the method returns TRUE if a valid EAIT group has been found, FALSE otherwise.
*/
void processGroup() {  
  int len = groupString.length();
  byte cks = 0; //checksum
  
  for( int i=0; i<(len-2); i++)
    cks = cks+groupString.charAt(i);

  cks = (cks & 0x3F) + 0x20;

  if (cks == groupString.charAt(len-1)) {
    //valid checksum, the group is valid
    if (groupString.indexOf("PAPP") == 0) {      
      papp = groupString.substring(5,11).toInt();  
    } else {
      if (groupString.indexOf("PTEC") == 0) {      
        ptec = groupString.substring(5,10);  
      }
    }
  }  
}


/*
 * In historic mode, the group is formatted as follow :
 * LF + LABEL* + SPC* + DATA* + SPC + CHECKSUM + CR
 * Field with the star are used to compute the checksum
 */
void makeFrame(){
  String group = "";
  String basePAPP = "00000";
  
  nextFrame = STX;
  
  group = LF;
  group += "ADCO 021861068854 ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;
  
  group = LF;
  group += "OPTARIF HC.. ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  group = LF;
  group += "ISOUSC 45 ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;
 
  group = LF;
  group += "HCHC ";
  group += "008979786";
  group += " ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  group = LF;
  group += "HCHP ";
  group += "013829433";
  group += " ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;
  
  group = LF;
  group += "PTEC ";
  group += ptec;
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  group = LF;
  group += "IINST 001 ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;
 
  group = LF;
  group += "IMAX 090 ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  basePAPP += papp;
  basePAPP = basePAPP.substring(basePAPP.length()-5);

  group = LF;
  group += "PAPP ";
  group += basePAPP;
  group += " ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  group = LF;
  group += "HHPHC A ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  group = LF;
  group += "MOTDETAT 000000 ";
  group += (char)checkSum(group);
  group += CR;
  nextFrame += group ;

  nextFrame += ETX;   
}

/*
 * In historic mode, the group is formatted as follow :
 * LF + LABEL* + SPC* + DATA* + SPC + CHECKSUM + CR
 * Field with the star are used to compute the checksum
 * 0123456789
 */
char checkSum(String &s){
  int len = s.length()-1;
  char cks = 0;
  for(int i=1; i<len; i++)
    cks += s.charAt(i);
  cks = (cks & 0x3F) + SPC;
  return cks;
}

void sendFrame() {
  static int index = 0;
  static long lastFrameSent = 0;

  //nothing to send because nothing received.
  if (currentFrame.length() == 0 && nextFrame.length() == 0)
    return;
    
  // the block below is executed only once, when the device get its firts valid frame
  if (currentFrame.length() == 0 && nextFrame.length() != 0) {
    currentFrame = nextFrame;
    nextFrame = "";
    index = 0; //just to be sure
  }

  // a delay between 16,7 and 33,4 ms must be inserted between 2 successive frames
  if (currentFrame.length() != 0 && (index == 0) && ((millis()-lastFrameSent) < 17) )
    return;
  else {
    //ConsoSpy.print(currentFrame.charAt(index));
    int tail = min(index+64, currentFrame.length());
    tail = min(index+Serial.availableForWrite(),tail);
    int i;
    for(i=index; i<tail; i++) {
      //Serial.print(currentFrame.charAt(index));
      Serial.print(currentFrame.charAt(i));
    }
    //index++;
    index = tail;
    if (index == currentFrame.length()) {
      //the current frame has been completely sent
      lastFrameSent = millis();
      index = 0;
      //is there a new frame pending ?
      if (nextFrame.length() != 0) {
        currentFrame = nextFrame;
        nextFrame = "";
      }
    }
  }
}
