package vrpn;
import java.util.*;


public class TempImagerRemote extends VRPN implements Runnable
{
	//////////////////
	// Public structures and interfaces
	public final static int VRPN_IMAGER_MAX_REGION = 31996;
	// this value is from vrpn_TempImager.h:  vrpn_CONNECTION_TCP_BUFLEN/sizeof(vrpn_int16) - 4
	//   or 64000 / 2 - 4 = 31996
	
	public final static int VRPN_IMAGER_MAX_CHANNELS = 10;
	// this value is directly from vrpn_TempImager.h
	
	public class TempImagerChannel
	{
		public String name = "";
		public String units = "";
		public float minVal = 0;
		public float maxVal = 0;
		public float offset = 0;
		public float scale = 0;
	}
	
	public class TempImagerRegion
	{
		public int channelIndex = -1;
		public int columnMin = -1, columnMax = -1;
		public int rowMin = -1, rowMax = -1;
		public short vals[] = new short[ VRPN_IMAGER_MAX_REGION ];	
	}
	
	
	public class TempImagerDescriptionUpdate
	{
		public Date msg_time = new Date();
	}
	
	public interface DescriptionChangeListener
	{
		public void tempImagerDescriptionUpdate( TempImagerDescriptionUpdate u, TempImagerRemote tempImager );
	}
	
	
	public class TempImagerRegionUpdate
	{
		public Date msg_time = new Date( );
		public TempImagerRegion region = null;
	}
		
	public interface RegionChangeListener
	{
		public void tempImagerRegionUpdate( TempImagerRegionUpdate v, TempImagerRemote tempImager );
	}
	
	// end of the public structures and interfaces
	//////////////////////////////////
	
	
	////////////////////////////////
	// Public methods
	//
	
	/**
	 * @exception java.lang.InstantiationException
	 *		If the tempImager could not be created because of problems with
	 *      its native code and linking.
	 */
	public TempImagerRemote( String name ) throws InstantiationException
	{
		try	
		{  
			synchronized( downInVrpnLock )
			{
				this.init( name );  
			}
		}
		catch( java.lang.UnsatisfiedLinkError e )
		{  
			System.out.println( "Error initializing remote tempImager " + name + "." );
			System.out.println( " -- Unable to find the right functions.  This may be a version problem." );
			throw new InstantiationException( e.getMessage( ) );
		}
		
		// initialize the regions
		
		
		this.tempImagerThread = new Thread( this );
		this.tempImagerThread.start( );
	}
	
	public int getNumRows( )
	{ return numRows; }
	
	public int getNumColumns( )
	{ return numColumns; }
	
	public float getMinX( )
	{ return minX; }
	
	public float getMaxX( )
	{ return maxX; }
	
	public float getMinY( )
	{ return minY; }
	
	public float getMaxY( )
	{ return maxY; }
	
	public int getNumChannels( )
	{ return numChannels; }
	
	public TempImagerChannel getChannel( int channel )
	{
		if( channel < 0 || channel > channels.length )
			return null;
		else
			return channels[channel];
	}
	
	
	public synchronized void addDescriptionChangeListener( DescriptionChangeListener listener )
	{
		descriptionListeners.addElement( listener );
	}

	/**
	 * @return true on success; false on failure
	 */
	public synchronized boolean removeDescriptionChangeListener( DescriptionChangeListener listener )
	{
		return descriptionListeners.removeElement( listener );
	}
	
	
	public synchronized void addRegionChangeListener( RegionChangeListener listener )
	{
		regionListeners.addElement( listener );
	}
	
	/**
	 * @return true on success; false on failure
	 */
	public synchronized boolean removeRegionChangeListener( RegionChangeListener listener )
	{
		return regionListeners.removeElement( listener );
	}
	
		
	/**
	 * Sets the interval between invocations of <code>mainloop()</code>, which checks
	 * for and delivers messages.
	 * @param period The period, in milliseconds, between the beginnings of two
	 * consecutive message loops.
	 */
	public synchronized void setTimerPeriod( long period )
	{
		mainloopPeriod = period;
	}
	
	/**
	 * @return The period, in milliseconds.
	 */
	public synchronized long getTimerPeriod( )
	{
		return mainloopPeriod;
	}
	
	
	/**
	 * Stops the tempImager thread
	 */
	public void stopRunning( )
	{
		keepRunning = false;
	}

	
	/**
	 * This should <b>not</b> be called by user code.
	 */
	public void run( )
	{
		while( keepRunning )
		{
			synchronized( downInVrpnLock )
			{
				this.mainloop( );
			}
			try { Thread.sleep( mainloopPeriod ); }
			catch( InterruptedException e ) { } 
		}
	}
	
	// end public methods
	////////////////////////
	
	
	////////////////////////
	// Protected methods
	//
	
	/**
	 * Should be called only by mainloop(), a native method which is itself
	 * synchronized.  By extension, this method is synchronized (in that, for
	 * a given tempImagerRemote object, this method can only be called from one
	 * thread at a time).
	 */
	protected void handleDescriptionChange( long tv_sec, long tv_usec )
	{
		// our values of numRows, numCols, minX, etc. should be set in the native
		// handler before this function is called.
		
		// putting the body of this function into a synchronized block prevents
		// other instances of TempImagerRemote from calling listeners' tempImagerDecriptionUpdate
		// method concurrently.
		synchronized( notifyingDescriptionListenersLock )
		{
			TempImagerDescriptionUpdate d = new TempImagerDescriptionUpdate();
			d.msg_time.setTime( tv_sec * 1000 + tv_usec );
			
			// notify all listeners
			Enumeration e = descriptionListeners.elements( );
			while( e.hasMoreElements( ) )
			{
				DescriptionChangeListener l = (DescriptionChangeListener) e.nextElement( );
				l.tempImagerDescriptionUpdate( d, this );
			}
		} // end synchronized( notifyingChangeListenersLock )
	} // end method handleDescriptionChange
	
	
	/**
	 * @see #handleTempImagerChange
	 */
	protected void handleRegionChange( long tv_sec, long tv_usec, int channelIndex, 
									   int columnMin, int columnMax, 
									   int rowMin, int rowMax )
	{
		synchronized( notifyingRegionListenersLock )
		{
			TempImagerRegionUpdate r = new TempImagerRegionUpdate( );
			r.msg_time.setTime( tv_sec * 1000 + tv_usec );
			r.region = new TempImagerRegion( );
			r.region.channelIndex = channelIndex;
			r.region.columnMin = columnMin;
			r.region.columnMax = columnMax;
			r.region.rowMin = rowMin;
			r.region.rowMax = rowMax;
			
			// regionValues should be set by the native code before
			// it calls this function
			r.region.vals = regionValues;
			
			// notify all listeners
			Enumeration e = regionListeners.elements( );
			while( e.hasMoreElements( ) )
			{
				RegionChangeListener l = (RegionChangeListener) e.nextElement( );
				l.tempImagerRegionUpdate( r, this );
			}
		} // end synchronized( notifyingRegionListenersLock )
	} // end method handleVelocityChange
		
	
	/**
	 * Initialize the native tempImager object
	 * @param name The name of the tempImager and host (e.g., <code>"TempImager0@myhost.edu"</code>).
	 * @return <code>true</code> if the tempImager was connected successfully, 
	 *			<code>false</code> otherwise.
	 */
	protected native boolean init( String name );

	/**
	 * This should only be called from the method finalize()
	 */
	protected native void shutdownTempImager( );
	
	protected synchronized native void mainloop( );
	
	protected void finalize( ) throws Throwable
	{
		keepRunning = false;
		while( tempImagerThread.isAlive( ) )
		{
			try { tempImagerThread.join( ); }
			catch( InterruptedException e ) { }
		}
		
		descriptionListeners.removeAllElements( );
		regionListeners.removeAllElements( );
		synchronized( downInVrpnLock )
		{
			this.shutdownTempImager( );
		}
	}
	
	
	protected void setupTempImagerDescription( int numRows, int numColumns,
											   float minX, float maxX, float minY,
											   float maxY, int numChannels )
	{
		this.numRows = numRows;
		this.numColumns = numColumns;
		this.minX = minX;
		this.maxX = maxX;
		this.minY = minY;
		this.maxY = maxY;
		this.numChannels = numChannels;
	}
	
	protected void setChannel( int index, String name, String units,
							   float minVal, float maxVal, float offset, float scale )
	{
		if( index < 0 || index >= VRPN_IMAGER_MAX_CHANNELS )
		{
			System.err.println( "Warning:  invalid channel index in TempImagerRemote::" +
								"setChannel.  Ignoring." );
			return;
		}
      if (channels[index] == null)
      {
         channels[index] = new TempImagerChannel();
      }
		channels[index].name = name;
		channels[index].units = units;
		channels[index].minVal = minVal;
		channels[index].maxVal = maxVal;
		channels[index].offset = offset;
		channels[index].scale = scale;
	}
	
	// end protected methods
	///////////////////////
	
	///////////////////
	// data members
	
	int numRows = 0;
	int numColumns = 0;
	float minX = 0;
	float maxX = 0;
	float minY = 0;
	float maxY = 0;
	int numChannels = 0;
	TempImagerChannel[] channels = new TempImagerChannel[VRPN_IMAGER_MAX_CHANNELS];
	
	// used for individual channels
	short[] regionValues = new short[VRPN_IMAGER_MAX_REGION];
	
	// this is used by the native code to store a C++ pointer to the 
	// native vrpn_TempImagerRemote object
	protected int native_tempImager = -1;
	
	// this is used to stop and to keep running the tracking thread
	// in an orderly fashion.
	protected boolean keepRunning = true;

	// the tracking thread
	Thread tempImagerThread = null;

	// how long the thread sleeps between checking for messages
	protected long mainloopPeriod = 100; // milliseconds
	
	protected Vector descriptionListeners = new Vector( );
	protected Vector regionListeners = new Vector( );
	
	/**
	 * these notifying*ListenersLock variables are used to ensure that multiple
	 * TempImagerRemote objects running in multiple threads don't call the 
	 * tempImagerChangeUpdate, et al, method of some single object concurrently.
	 * For example, the handleTempImagerChange(...) method, which is invoked from native 
	 * code, gets a lock on the notifyingChangeListenersLock object.  Since that object
	 * is static, all other instances of TempImagerRemote must wait before notifying 
	 * their listeners and completing their handleTempImagerChange(...) methods.
	 * They are necessary, in part, because methods in an interface can't be declared
	 * synchronized (and the semantics of the keyword 'synchronized' aren't what's
	 * wanted here, anyway -- we want synchronization across all instances, not just a 
	 * single object).
	 */
	protected final static Object notifyingDescriptionListenersLock = new Object( );
	protected final static Object notifyingRegionListenersLock = new Object( );
	
	
}